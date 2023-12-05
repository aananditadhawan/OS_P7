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

static int wfs_getattr(const char *path, struct stat *stbuf) {
  int res;

  res = lstat(path, stbuf);
  if (res == -1)
    return -errno;

  return 0;
}

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
}

static int wfs_write(const char* path, char *buf, size_t size, off_t offset, struct fuse_file_info* fi) {
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
