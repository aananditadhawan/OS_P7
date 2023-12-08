/*#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include "wfs.h"

struct wfs_sb *superblock;

void initializeSuperblock() {
    superblock = (struct wfs_sb *) malloc(sizeof(struct wfs_sb));
    superblock->magic = WFS_MAGIC;
    superblock->head = sizeof(struct wfs_sb);

    struct wfs_log_entry *root = (struct wfs_log_entry *) malloc(sizeof(struct wfs_inode));
    root->inode.inode_number = 0;

    superblock->head += sizeof(root);
}

void writeSuperblock(int fd) {
    if (lseek(fd, 0, SEEK_SET) == -1) {
        perror("Error seeking to the beginning of the file");
        exit(EXIT_FAILURE);
    }

    if (write(fd, superblock, sizeof(struct wfs_sb)) == -1) {
        perror("Error writing superblock");
        exit(EXIT_FAILURE);
    }

}

void initializeLogEntry(struct wfs_log_entry *log_entry, const char *name, unsigned int mode) {
    // Initialize the log entry
    log_entry->inode.deleted = 0;
    log_entry->inode.mode = mode; // Set mode based on the file type
    log_entry->inode.size = 0; // Initialize size to 0
    log_entry->inode.links = 1; // Set links to 1

    // Set other inode attributes as needed

    // Initialize the dentry
    strncpy(log_entry->data, name, MAX_FILE_NAME_LEN);
    log_entry->data[MAX_FILE_NAME_LEN - 1] = '\0'; // Ensure null-termination
}

void writeLogEntry(int fd, struct wfs_log_entry *log_entry) {
    if (write(fd, log_entry, sizeof(struct wfs_log_entry) + MAX_FILE_NAME_LEN) == -1) {
        perror("Error writing log entry");
        exit(EXIT_FAILURE);
    }

    superblock->head += sizeof(struct wfs_log_entry);
}

int main(int argc, char *argv[]) {
    // Check if the correct number of arguments is provided
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <disk_path>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    // Get the disk path from the command line arguments
    const char *disk_path = argv[1];

    // Open the file in binary read-write mode, create it if it doesn't exist
    //int fd = open(disk_path, O_CREAT | O_RDWR, 0644);
    int fd = open(disk_path, O_RDWR, 0644);
    if (fd == -1) {
        perror("Error opening file");
        exit(EXIT_FAILURE);
    }

    char buffer[1024]; // Adjust the size as needed
    ssize_t bytesRead = read(fd, buffer, sizeof(buffer));

    // Initialize the superblock
    initializeSuperblock();

    // Write the superblock to the file
    writeSuperblock(fd);

    // malloc cant have size of struct log entry 
    struct wfs_log_entry *file_log_entry = (struct wfs_log_entry *)malloc(sizeof(struct wfs_inode) + bytesRead); // malloc this
    
    //struct wfs_log_entry file_log_entry;
    initializeLogEntry(file_log_entry, "example_file.txt", 0); // S_IFREG for regular file
    writeLogEntry(fd, file_log_entry);

    // Example: Initialize and write a log entry for a directory
    // struct wfs_log_entry dir_log_entry;
    // initializeLogEntry(&dir_log_entry, "example_directory", 1); // S_IFDIR for directory
    // writeLogEntry(fd, &dir_log_entry);

    // Close the file
    if (close(fd) == -1) {
        perror("Error closing file");
        exit(EXIT_FAILURE);
    }

    printf("Filesystem initialized successfully.\n");

    return 0;
}*/

#define _POSIX_C_SOURCE 200809L
#include "wfs.h"
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

static int init_filesystem(const char *path) {
    // Open the file for read-write, create if not exists, truncate to zero length
    int fd = open(path, O_RDWR | O_CREAT | O_TRUNC, 0644);
    if (fd == -1) {
        perror("Error opening file");
        return -1;
    }

    // Initialize the superblock
    struct wfs_sb superblock = {
        .magic = WFS_MAGIC,
        .head = sizeof(struct wfs_sb) + sizeof(struct wfs_inode) // Start of the next available space
    };

    // Write the superblock to the file
    if (write(fd, &superblock, sizeof(struct wfs_sb)) == -1) {
        perror("Error writing superblock");
        close(fd);
        return -1;
    }

    // Create the root inode
    struct wfs_inode root_inode = {
        .inode_number = 0,        // Root has inode number 0
        .deleted = 0,             // Root is not deleted
        .mode = S_IFDIR | 0755,   // Root is a directory with default permissions
        .uid = getuid(),          // User id of the owner
        .gid = getgid(),          // Group id of the owner
        .flags = 0,               // Set the appropriate flags
        .size = 0,                // Initialize size to 0 for a directory
        .atime = time(NULL),      // Set the current time as the access time
        .mtime = time(NULL),      // Set the current time as the modify time
        .ctime = time(NULL),      // Set the current time as the inode change time
        .links = 1                // Number of hard links to this file (always set to 1)
    };

    // Write the root inode to the file
    if (write(fd, &root_inode, sizeof(struct wfs_inode)) == -1) {
        perror("Error writing root inode");
        close(fd);
        return -1;
    }

    // Close the file
    close(fd);

    printf("Filesystem initialized successfully at %s\n", path);
    return 0;
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <disk_path>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    const char *disk_path = argv[1];

    // Initialize the filesystem
    if (init_filesystem(disk_path) == -1) {
        fprintf(stderr, "Failed to initialize filesystem.\n");
        exit(EXIT_FAILURE);
    }

    return 0;
}

