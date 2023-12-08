#define FUSE_USE_VERSION 30
#include <fuse.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "wfs.h"
#include <unistd.h> 
#include <errno.h>
#include <dirent.h>
#include <sys/mman.h>

// Declare global variables for the superblock and file descriptors
static struct wfs_map md = {0};
struct wfs_sb superblock;
int disk_fd;
int inode_glbl_number = 1;
char dir_list[256][256];
int curr_dir_idx = -1;

char files_list[256][256];
int curr_file_idx = -1;

char files_content[256][256];
int curr_file_content_idx = -1;
// the following is already there in fuse.h
//typedef int (*fuse_fill_dir_t) (void *buf, const char *name, const struct stat *stbuf, off_t off);

static struct wfs_inode *get_inode(unsigned int inode_number) {
    char *pos = sizeof(struct wfs_sb) + (char *)md.disk;
    struct wfs_log_entry *cur_ent;

    struct wfs_inode *last_ent = NULL;
    while ((uintptr_t)pos < (uintptr_t)md.disk + md.head) {

        cur_ent = (struct wfs_log_entry *)pos;

        if (cur_ent->inode.inode_number == inode_number && cur_ent->inode.deleted == 0) {
            last_ent = &(cur_ent->inode);
        }

        pos += sizeof(struct wfs_inode) + cur_ent->inode.size;
    }

    return last_ent;
}

static unsigned long get_inumber(const char *path) {
    unsigned long inode_num = 0;

    char path_copy[strlen(path) + 1];
    strcpy(path_copy, path);

    char *sign = strtok(path_copy, "/");
    int flag = 1; 
    while (sign != NULL) {
        flag = 0;
        char *pos = sizeof(struct wfs_sb) + (char *)md.disk;
        struct wfs_log_entry *cur_ent;

        struct wfs_log_entry *last_ent;
        while ((uintptr_t)pos < (uintptr_t)md.disk + md.head) {
            cur_ent = (struct wfs_log_entry *)pos;

            if (cur_ent->inode.inode_number == inode_num && cur_ent->inode.deleted == 0 && S_ISDIR(cur_ent->inode.mode)) {
                last_ent = cur_ent;
            }

            pos += cur_ent->inode.size + sizeof(struct wfs_inode);
        }
        
	struct wfs_dentry *dir = (struct wfs_dentry *)last_ent->data;
        int doffset = 0;
        while (doffset < last_ent->inode.size) {
            if (strcmp(dir->name, sign) == 0) {
                inode_num = dir->inode_number;
		flag = 1;
                break;
            }
	    dir++;
            doffset += sizeof(struct wfs_dentry); 
        }

        sign = strtok(NULL, "/");
    }
    if (flag == -1) {
        return -1;
    }

    return inode_num;
}


static int wfs_getattr(const char *path, struct stat *stbuf) {
  stbuf->st_uid = getuid();
  stbuf->st_gid = getgid();
  stbuf->st_mtime = time(NULL); 

  int dir = 0;
  int file = 0;
  path++; 
  for (int curr_idx = 0; curr_idx <= curr_dir_idx; curr_idx++) {
    if (strcmp(path, dir_list[curr_idx]) == 0) {
      dir = 1;
    } 
  }

  for (int curr_idx = 0; curr_idx <= curr_file_idx; curr_idx++) {
    if (strcmp(path, files_list[curr_idx]) == 0) {
       file = 1;
    } 
  }

  if (strcmp(path, "/") == 0 || dir == 1) {
    stbuf->st_mode = S_IFDIR | 0755;
    stbuf->st_nlink = 2; 
  } else if (file == 1) {
    stbuf->st_mode = S_IFREG | 0644;
    stbuf->st_size = 1024;
    stbuf->st_nlink = 1;
  } else {
    return -ENOENT;
  }

  return 0; 
}

static int wfs_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi) {
    unsigned long inode_num = get_inumber(path);

    if (inode_num == -1) {
        return -ENOENT; 
    }

    struct wfs_inode *inode = get_inode(inode_num);

    if (inode == NULL) {
        return -ENOENT;
    }

    if (!S_ISREG(inode->mode)) {
        return -EISDIR; 
    }

    /*if (offset >= inode->size) {
        return 0;
    }*/

    size_t max_rsize = inode->size - offset;
    size_t rsize;
    if (size < max_rsize) {
      rsize = size;
    } else {
      rsize = max_rsize;
    }
    char *pos = sizeof(struct wfs_sb) + (char *)md.disk;
    struct wfs_log_entry *cur_ent;
    struct wfs_log_entry *last_ent;
    while ((uintptr_t)pos < (uintptr_t)md.disk + md.head) {
        cur_ent = (struct wfs_log_entry *)pos;

        if (cur_ent->inode.inode_number == inode_num && cur_ent->inode.deleted == 0 && S_ISREG(cur_ent->inode.mode)) {
            last_ent = cur_ent;
        }

        pos += sizeof(struct wfs_inode) + cur_ent->inode.size;
    }

    memcpy(buf, last_ent->data + offset, size);

    return rsize; 
}

static int wfs_write(const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *fi) {
  path++; 
  int file_idx = -1;
  for (int i = 0; i <= curr_file_idx; i++)
    if (strcmp(path, files_list[i]) == 0)
      file_idx = i;

  if (file_idx == -1) 
    return -ENOENT;

  strcpy(files_content[file_idx], buf);
  return size;
}

static int wfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi) {
    unsigned long inode_num = get_inumber(path);
    if (inode_num == -1) {
        return -ENOENT; 
    }

    struct wfs_inode *inode = get_inode(inode_num);
    if (inode == NULL) {
        return -ENOENT; 
    }

    if (!S_ISDIR(inode->mode)) {
        return -ENOTDIR; 
    }

    char *pos = sizeof(struct wfs_sb) + (char *)md.disk;
    struct wfs_log_entry *cur_ent;
    struct wfs_log_entry *last_ent;
    while ((uintptr_t)pos < (uintptr_t)md.disk + md.head) {
        cur_ent = (struct wfs_log_entry *)pos;

        if (cur_ent->inode.inode_number == inode_num && cur_ent->inode.deleted == 0 && S_ISDIR(cur_ent->inode.mode)) {
            last_ent = cur_ent;
        }

        pos += sizeof(struct wfs_inode) + cur_ent->inode.size;
    }

    struct wfs_dentry *dir = (struct wfs_dentry *)last_ent->data;
    int doffset = 0;
    while (doffset < last_ent->inode.size) {
        filler(buf, dir->name, NULL, 0);
        dir++;
	doffset += sizeof(struct wfs_dentry);
    }

    return 0;
}

static int wfs_mknod(const char* path, mode_t mode, dev_t rdev) {
  path++;
  curr_file_idx++;
  strcpy(files_list[curr_file_idx], path);

  curr_file_content_idx++;
  strcpy(files_content[curr_file_content_idx], "");
  return 0;
}

static int wfs_mkdir(const char* path, mode_t mode) {
  path++;
  curr_dir_idx++;
  strcpy(dir_list[curr_dir_idx], path);

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
    if (argc < 3 || argv[argc - 2][0] == '-' || argv[argc - 1][0] == '-') {
        fprintf(stderr, "Usage: %s [FUSE options] disk_path mount_point\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    const char *disk_path = argv[argc - 2];

    argv[argc - 2] = argv[argc - 1];
    argv[argc - 1] = NULL;
    argc--;

    md.fd = open(disk_path, O_RDWR);
    if (md.fd < 0) {
        perror("File does not exist");
        return errno;
    }

    struct stat st;
    stat(disk_path, &st);

    md.disk = mmap(0, st.st_size, PROT_READ | PROT_WRITE, MAP_SHARED, md.fd, 0);
    if (md.disk == MAP_FAILED) {
        perror("Error mmap");
        return errno;
    }

    struct wfs_sb *sb = md.disk;

    /*if (sb->magic != WFS_MAGIC) {
        perror("Not a wfs filesystem. Not mounted.\n");
        goto FINISH;
    }*/

    md.len = st.st_size;
    md.head = sb->head;
    fuse_main(argc, argv, &wfs_operations, NULL);
    sb->head = md.head;


    munmap(md.disk, md.len);
    close(md.fd);
    return 0;

}
