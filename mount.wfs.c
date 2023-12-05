#define FUSE_USE_VERSION 30
#include <fuse.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "wfs.h"
#include <unistd.h> 

// Declare global variables for the superblock and file descriptors
struct wfs_sb superblock;
int disk_fd;

// the following is already there in fuse.h
//typedef int (*fuse_fill_dir_t) (void *buf, const char *name, const struct stat *stbuf, off_t off);

static int wfs_getattr(const char *path, struct stat *stbuf) {
    // Implement the getattr FUSE callback
    // This function should fill in the attributes of the file/directory specified by the path
    // Use the superblock and disk_fd global variables

    int res = lstat(path, stbuf);

    return res;  // Return 0 on success
}

static int wfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi) {
    // Implement the readdir FUSE callback
    // This function should fill in the directory entries using the filler function
    // Use the superblock and disk_fd global variables

    printf( "--> Getting The List of Files of %s\n", path );
	
	filler(buf, ".", NULL, 0 ); // Current Directory
	filler(buf, "..", NULL, 0 ); // Parent Directory
	
    return 0;  // Return 0 on success
}

static int wfs_open(const char *path, struct fuse_file_info *fi) {
    // Implement the open FUSE callback
    // This function should open the file specified by the path and update the fi structure
    // Use the superblock and disk_fd global variables

    return 0;  // Return 0 on success
}

static int wfs_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi) {
    // Implement the read FUSE callback
    // This function should read data from the file specified by the path
    // Use the superblock and disk_fd global variables

    return 0;  // Return 0 on success
}

static struct fuse_operations wfs_operations = {
    .getattr = wfs_getattr,
    .readdir = wfs_readdir,
    .open = wfs_open,
    .read = wfs_read,
};

int main(int argc, char *argv[]) {
    // Check if the correct number of arguments is provided
    if (argc < 4) {
        fprintf(stderr, "Usage: %s [FUSE options] disk_path mount_point\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    // Open the file in binary read-only mode
    disk_fd = open(argv[2], O_RDONLY);
    if (disk_fd == -1) {
        perror("Error opening file");
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

    // Pass [FUSE options] along with the mount_point to fuse_main as argv
    argv[2] = argv[3];
    argv[3] = NULL;  // Null-terminate the new argv

    // Mount the file system using FUSE
    return fuse_main(argc - 2, argv, &wfs_operations, NULL);
}
