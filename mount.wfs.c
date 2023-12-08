#define FUSE_USE_VERSION 30
#include <fuse.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "wfs.h"
#include <unistd.h> 
#include <errno.h>
#include <dirent.h>

// Declare global variables for the superblock and file descriptors
struct wfs_sb superblock;
int disk_fd;
int inode_glbl_number = 1;

// the following is already there in fuse.h
//typedef int (*fuse_fill_dir_t) (void *buf, const char *name, const struct stat *stbuf, off_t off);

static int read_log_entry(FILE *disk, unsigned long position, struct wfs_log_entry *entry) {
    if (fseek(disk, position, SEEK_SET) != 0) {
        perror("Error seeking in disk file");
        return -1;
    }

    // Read the inode part of the log entry
    if (fread(&entry->inode, sizeof(struct wfs_inode), 1, disk) != 1) {
        perror("Error reading inode from disk file");
        return -1;
    }

    // Allocate memory for the data part of the log entry, if necessary
    if (entry->inode.size > 0) {
        char *data = malloc(entry->inode.size);
        if (data == NULL) {
            perror("Memory allocation failed");
            return -1;
        }

        if (fread(data, 1, entry->inode.size, disk) != entry->inode.size) {
            perror("Error reading data from disk file");
            free(data);
            return -1;
        }

        // Using casting to bypass the restriction on flexible array member
        *((char **)&entry->data) = data;
    } else {
        // Use casting to set the flexible array member to NULL
        *((char **)&entry->data) = NULL;
    }

    return 0;
}

static int write_log_entry(const struct wfs_log_entry *entry) {
    int fd = open("path_to_your_disk_image", O_RDWR);
    if (fd == -1) {
        perror("Error opening disk image");
        return -1;
    }

    // Seek to the end of the file to append the log entry
    if (lseek(fd, 0, SEEK_END) == -1) {
        perror("Error seeking to end of disk image");
        close(fd);
        return -1;
    }

    // Write the inode part of the log entry
    if (write(fd, &entry->inode, sizeof(struct wfs_inode)) == -1) {
        perror("Error writing inode to disk image");
        close(fd);
        return -1;
    }

    // Write the data part of the log entry if it exists
    if (entry->inode.size > 0 && entry->data != NULL) {
        if (write(fd, entry->data, entry->inode.size) == -1) {
            perror("Error writing data to disk image");
            close(fd);
            return -1;
        }
    }

    // Update the superblock to reflect the new head of the log
    struct wfs_sb superblock;
    if (pread(fd, &superblock, sizeof(struct wfs_sb), 0) != sizeof(struct wfs_sb)) {
        perror("Error reading superblock from disk image");
        close(fd);
        return -1;
    }

    superblock.head += sizeof(struct wfs_inode) + entry->inode.size;
    if (pwrite(fd, &superblock, sizeof(struct wfs_sb), 0) != sizeof(struct wfs_sb)) {
        perror("Error writing updated superblock to disk image");
        close(fd);
        return -1;
    }

    close(fd);
    return 0;
}

static size_t calculate_log_entry_size(FILE *disk, unsigned long position) {
    // Seek to the position in the disk file
    if (fseek(disk, position - sizeof(struct wfs_inode), SEEK_SET) != 0) {
        perror("Error seeking in disk file");
        return 0;
    }

    // Read the inode part of the log entry
    struct wfs_inode inode;
    if (fread(&inode, sizeof(struct wfs_inode), 1, disk) != 1) {
        perror("Error reading inode from disk file");
        return 0;
    }

    // Calculate the total size of the log entry (inode size + data size)
    return sizeof(struct wfs_inode) + inode.size;
}

static struct wfs_log_entry *find_latest_log_entry_for_inode(FILE *disk, uint inode_number) {
    // Starting position of the log entries in the disk, typically after the superblock
    unsigned long start_position = sizeof(struct wfs_sb); // Replace with actual start position

    // Assuming the end of the log is at the current position of the disk file
    fseek(disk, 0, SEEK_END);
    unsigned long current_position = ftell(disk);

    struct wfs_log_entry *latest_entry = NULL;

    while (current_position > start_position) {
        // Move to the start of the previous inode
        current_position -= sizeof(struct wfs_inode);

        // Calculate the size of the current log entry
        size_t entry_size = calculate_log_entry_size(disk, current_position);
        if (entry_size == 0) {
            // Handle error in calculating size or break if it's an expected end condition
            perror("Error calculating log entry size");
            exit(EXIT_FAILURE);
        }

        // Adjust current position to the start of the log entry
        current_position -= entry_size - sizeof(struct wfs_inode);

        struct wfs_log_entry entry;
        if (read_log_entry(disk, current_position, &entry) != 0) {
            // If reading the log entry fails, handle the error appropriately
	    perror("Error reading log entry");
            exit(EXIT_FAILURE);
        }

        if (entry.inode.inode_number == inode_number && !entry.inode.deleted) {
            // Found a log entry for the inode number
            if (latest_entry == NULL) {
                latest_entry = malloc(sizeof(struct wfs_log_entry) + entry.inode.size);
                if (latest_entry == NULL) {
                    perror("Memory allocation failed");
                    return NULL;
                }
            }
            memcpy(latest_entry, &entry, sizeof(struct wfs_inode) + entry.inode.size);
        }
    }

    return latest_entry;
}

static unsigned long get_inumber(const char *path) {
    if (strcmp(path, "/") == 0) {
        return 0; // The root directory always has inode number 0
    }

    FILE *disk = fopen("path_to_your_disk_image", "rb"); // Open the disk image file
    if (disk == NULL) {
        perror("Unable to open disk image");
        return -1;
    }

    struct wfs_log_entry entry;
    if (read_log_entry(disk, 0, &entry) != 0) { // Start with the root directory inode
        fprintf(stderr, "Error reading root directory log entry\n");
        fclose(disk);
        return -1;
    }

    char *token;
    char *rest = strdup(path);

    while ((token = strtok_r(rest, "/", &rest))) {
        // Iterate over each component in the path
        // For each component, find the corresponding dentry in the directory log entry

        int found = 0;
        for (int i = 0; i < entry.inode.size / sizeof(struct wfs_dentry); ++i) {
            struct wfs_dentry *dentry = (struct wfs_dentry *)(entry.data + i * sizeof(struct wfs_dentry));
            if (strcmp(dentry->name, token) == 0) {
                // Found the dentry, read the corresponding inode
                if (read_log_entry(disk, dentry->inode_number, &entry) != 0) {
                    fprintf(stderr, "Error reading log entry for inode %lu\n", dentry->inode_number);
                    free(rest);
                    fclose(disk);
                    return -1;
                }
                found = 1;
                break;
            }
        }

        if (!found) {
            fprintf(stderr, "Path component not found: %s\n", token);
            free(rest);
            fclose(disk);
            return -1;
        }
    }

    unsigned long inumber = entry.inode.inode_number;
    free(rest);
    fclose(disk);
    return inumber;
}

static struct wfs_inode *get_inode(uint inode_number) {
    FILE *disk = fopen("path_to_your_disk_image", "rb"); // Open the disk image file
    if (disk == NULL) {
        perror("Unable to open disk image");
        return NULL;
    }

    struct wfs_log_entry *latest_entry = find_latest_log_entry_for_inode(disk, inode_number);
    if (latest_entry == NULL) {
        fprintf(stderr, "No live inode found for inode number %u\n", inode_number);
        fclose(disk);
        return NULL;
    }

    struct wfs_inode *inode = malloc(sizeof(struct wfs_inode));
    if (inode == NULL) {
        perror("Memory allocation failed");
        fclose(disk);
        return NULL;
    }

    *inode = latest_entry->inode;

    fclose(disk);
    return inode;
}


static int wfs_getattr(const char *path, struct stat *stbuf) {
  /*int res;

  res = lstat(path, stbuf);
  if (res == -1)
    return -errno;

  return 0;*/
  memset(stbuf, 0, sizeof(struct stat));

  // Get inode number from the given path
  unsigned long inumber = get_inumber(path);
  if (inumber == (unsigned long)(-1)) {
    return -ENOENT;  // No such file or directory
  }

  // Get inode data 
  struct wfs_inode *inode = get_inode(inumber);
  if (!inode) {
    return -ENOENT;  // Handle error if inode is not found
  }

  // Fill the stat structure
  stbuf->st_mode = inode->mode;
  stbuf->st_nlink = inode->links;
  stbuf->st_uid = inode->uid;
  stbuf->st_gid = inode->gid;
  stbuf->st_size = inode->size;
  stbuf->st_atime = inode->atime;
  stbuf->st_mtime = inode->mtime;
  stbuf->st_ctime = inode->ctime;

  free(inode);  // Free the allocated inode

  return 0;
  
}

/*static int wfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi) {
    (void) offset; // Unused parameter
    (void) fi;     // Unused parameter

    // Get inode number from the path
    unsigned long inumber = get_inumber(path);
    if (inumber == (unsigned long)(-1)) {
        return -ENOENT; // Directory not found
    }

    // Read the directory inode
    struct wfs_log_entry *entry;
    FILE *disk = fopen(disk_fd, "rb");  // Open the disk image file
    if (!disk) {
      perror("Error opening disk image");
      return -EIO;  // I/O error
    }

    unsigned long position = get_inode_position(inumber);  // You need to implement this function
    if (position == (unsigned long)(-1)) {
      fclose(disk);
      return -EIO;  // I/O error or inode not found
    }

    if (read_log_entry(disk, position, &entry) != 0) {
      fclose(disk);
      return -EIO;  // I/O error
    }
    if (read_log_entry(inumber, &entry) != 0) {
        return -EIO; // Input/output error
    }

    // Check if it's actually a directory
    if ((entry->inode.mode & S_IFDIR) == 0) {
        free(entry);
        return -ENOTDIR; // Not a directory
    }

    // Iterate over the directory entries
    struct wfs_dentry *dentry = (struct wfs_dentry *)entry->data;
    for (size_t i = 0; i < entry->inode.size / sizeof(struct wfs_dentry); ++i) {
        // Use the filler function to fill the read directory entries into the buffer
        if (filler(buf, dentry[i].name, NULL, 0) != 0) {
            free(entry);
            return -ENOMEM; // No memory
        }
    }
    
    free(entry);
    fclose(disk);
    return 0;
}*/


static int wfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi) {
  DIR *dp;
  struct dirent *de;
  (void) offset;
  (void) fi;

  dp = opendir(path);
  if (dp == NULL)
    return -errno;

  while ((de = readdir(dp)) != NULL) {
    struct stat st;
    memset(&st, 0, sizeof(st));	
    st.st_ino = de->d_ino;	
    st.st_mode = de->d_type << 12;
    if (filler(buf, de->d_name, &st, 0))	
      break;
    }
    closedir(dp);	
    return 0;
    
    printf( "--> Getting The List of Files of %s\n", path );
	
	filler(buf, ".", NULL, 0 ); // Current Directory
	filler(buf, "..", NULL, 0 ); // Parent Directory
	
    return 0;  // Return 0 on success

}

static int wfs_write(const char* path, const char *buf, size_t size, off_t offset, struct fuse_file_info* fi) {
  int fd;
  int res;

  (void) fi;
  fd = open(path, O_WRONLY);
  if (fd == -1)
    return -errno;

  res = pwrite(fd, buf, size, offset);
  if (res == -1)
    res = -errno;

  close(fd);
  return res;
}

static int wfs_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi) {
  int fd;
  int res;

  (void) fi;
  fd = open(path, O_RDONLY);
  if (fd == -1)
    return -errno;

  res = pread(fd, buf, size, offset);
  if (res == -1)
    res = -errno;

  close(fd);
  return res;
}

static int wfs_mknod(const char* path, mode_t mode, dev_t rdev) {
  int res;

  if (S_ISREG(mode)) {
    res = open(path, O_CREAT | O_EXCL | O_WRONLY, mode);
  if (res >= 0)
    res = close(res);
  } else if (S_ISFIFO(mode))
    res = mkfifo(path, mode);
  else		
    res = mknod(path, mode, rdev);
  if (res == -1)
    return -errno;

  return 0;
}

/*static int wfs_mkdir(const char *path, mode_t mode) {
    // Split the path into parent directory and new directory name
    char *parent_path = strdup(path);
    char *dir_name = strrchr(parent_path, '/');
    if (!dir_name) {
        free(parent_path);
        return -ENOENT; // No such file or directory
    }
    *dir_name++ = '\0'; // Split the path and move dir_name to point to the directory name

    // Get the inode number of the parent directory
    unsigned long parent_inumber = get_inumber(parent_path);
    if (parent_inumber == (unsigned long)(-1)) {
        free(parent_path);
        return -ENOENT; // Parent directory not found
    }

    // Read the parent directory's log entry
    struct wfs_log_entry *parent_entry;
    if (read_log_entry(parent_inumber, &parent_entry) != 0) {
        free(parent_path);
        return -EIO; // I/O error
    }

    // Check if the parent is indeed a directory
    if ((parent_entry->inode.mode & S_IFDIR) == 0) {
        free(parent_entry);
        free(parent_path);
        return -ENOTDIR; // Parent is not a directory
    }

    // Create a new inode for the directory
    struct wfs_inode new_dir_inode = {
        .inode_number = inode_glbl_number,
        .deleted = 0,
        .mode = S_IFDIR | mode, // Set the mode to directory with given permissions
        .uid = getuid(),
        .gid = getgid(),
        .size = 0, // Initially, the size is 0
        .atime = time(NULL),
        .mtime = time(NULL),
        .ctime = time(NULL),
        .links = 1
    };
    inode_glbl_number++;

    // Write the new directory's log entry
    if (write_log_entry(&new_dir_inode) != 0) {
        free(parent_entry);
        free(parent_path);
        return -EIO; // I/O error
    }

    // Update the parent directory's log entry to include the new directory
    // This involves writing a new log entry for the parent with the additional directory entry
    // ...

    // free(parent_entry);
    // free(parent_path);
    // return 0;
    
    struct wfs_dentry new_dentry = {
      .inode_number = new_dir_inode.inode_number,
      .name = {0}
    };
    strncpy(new_dentry.name, dir_name, MAX_FILE_NAME_LEN - 1);

    // Calculate the size needed for the updated parent directory data (existing data + new dentry)
    size_t new_data_size = parent_entry->inode.size + sizeof(struct wfs_dentry);
    char *new_data = malloc(new_data_size);
    if (!new_data) {
      free(parent_entry);
      free(parent_path);
      return -ENOMEM; // Not enough memory
    }

  // Copy the existing data
  memcpy(new_data, parent_entry->data, parent_entry->inode.size);
  // Add the new directory entry at the end
  memcpy(new_data + parent_entry->inode.size, &new_dentry, sizeof(struct wfs_dentry));

  // Create a new log entry for the parent directory with the updated data
  struct wfs_log_entry new_parent_entry = parent_entry->inode;
  new_parent_entry.inode.size = new_data_size;
  new_parent_entry.data = new_data;

  // Write the updated parent directory's log entry
  if (write_log_entry(&new_parent_entry) != 0) {
    free(new_data);
    free(parent_entry);
    free(parent_path);
    return -EIO; // I/O error
  }

  free(new_data);
  free(parent_entry);
  free(parent_path);

  return 0;
}*/

static int wfs_mkdir(const char* path, mode_t mode) {
  int res;

  res = mkdir(path, mode);
  if (res == -1)
    return -errno;

  return 0;  
}

static int wfs_unlink(const char* path) {
  int res;

  res = unlink(path);
  if (res == -1)
    return -errno;

  return 0;
}

static struct fuse_operations wfs_operations = {
    .getattr	= wfs_getattr,
    .mknod      = wfs_mknod,
    .mkdir      = wfs_mkdir,
    .read	= wfs_read,
    .write      = wfs_write,
    .readdir	= wfs_readdir,
    .unlink    	= wfs_unlink,

};

int main(int argc, char *argv[]) {
    // Check if the correct number of arguments is provided
    // if (argc < 4) {
    //     fprintf(stderr, "Usage: %s [FUSE options] disk_path mount_point\n", argv[0]);
    //     exit(EXIT_FAILURE);
    // }

    if (argc < 3) {
        fprintf(stderr, "Usage: %s [FUSE options] disk_path mount_point\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    // Identify the disk path and mount point based on the provided arguments
    const char *disk_path = argv[argc - 2];
    //const char *mount_point = argv[argc - 1];

    //printf("argument 3 is %s", argv[3]);

    //const char *disk_path = argv[3];

    int disk_fd = open(disk_path, O_RDWR, 0644);
    if (disk_fd == -1) {
        perror("rom mount main accessing the disk : Error opening file");
        exit(EXIT_FAILURE);
    }

    // Read the superblock from the file
    if (read(disk_fd, &superblock, sizeof(struct wfs_sb)) == -1) {
        perror("Error reading superblock");
        close(disk_fd);
        exit(EXIT_FAILURE);
    }

    // Close the file
    close(disk_fd);

    // // Pass [FUSE options] along with the mount_point to fuse_main as argv
    // argv[2] = argv[3];
    // argv[3] = NULL;  // Null-terminate the new argv

    argv[argc-2] = argv[argc-1];
    argv[argc-1] = NULL;
    argc--;

    // Mount the file system using FUSE
    return fuse_main(argc, argv, &wfs_operations, NULL);
}
