#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include "vsfs.h"

#define MAX_OPEN_FILES 16

struct superblock {
    int blockSize;
    int blockCount;
    int freeBlockCount;
    int reservedBlockCount;
    int firstFreeBlock;
    int directoryEntryCount;
    int FATEntryCount;
    int firstFreeDirectoryEntry;
    int firstFreeFATEntry;
};

struct FATentry{
    int next;
};

struct directoryEntry{
    char filename[31];
    int size;
    int firstBlock;
    char padding[89];
};

struct openFileEntry{
    char filename[31];
    char mode;
    int position;
};

// globals  =======================================
int vs_fd; // file descriptor of the Linux file that acts as virtual disk.
// Global structures for the superblock, FAT table, and root directory
struct superblock sb;
struct FATentry *fat = NULL;
struct directoryEntry *root = NULL;
struct openFileEntry openFiles[MAX_OPEN_FILES];
// ========================================================


// read block k from disk (virtual disk) into buffer block.
// size of the block is BLOCKSIZE.
// space for block must be allocated outside of this function.
// block numbers start from 0 in the virtual disk. 
int read_block (void *block, int k)
{
    int n;
    int offset;

    offset = k * BLOCKSIZE;
    lseek(vs_fd, (off_t) offset, SEEK_SET);
    n = read (vs_fd, block, BLOCKSIZE);
    if (n != BLOCKSIZE) {
	printf ("read error\n");
	return -1;
    }
    return (0); 
}

// write block k into the virtual disk. 
int write_block (void *block, int k)
{
    int n;
    int offset;

    offset = k * BLOCKSIZE;
    lseek(vs_fd, (off_t) offset, SEEK_SET);
    n = write (vs_fd, block, BLOCKSIZE);
    if (n != BLOCKSIZE) {
	printf ("write error\n");
	return (-1);
    }
    return 0; 
}


/**********************************************************************
   The following functions are to be called by applications directly. 
***********************************************************************/

int vsformat (char *vdiskname, unsigned int m)
{
    char command[1000];
    int size;
    int count;
    size  = 1 << m;
    count = size / BLOCKSIZE;
    //    printf ("%d %d", m, size);
    sprintf (command, "dd if=/dev/zero of=%s bs=%d count=%d",
             vdiskname, BLOCKSIZE, count);
    printf ("executing command = %s\n", command);
    system (command);

    // Write the superblock to the virtual disk
    if (write(vs_fd, &sb, sizeof(sb)) != sizeof(sb)) {
        perror("Error writing superblock to virtual disk");
        return -1;
    }

    // Initialize and write the FAT to the virtual disk
    struct FATentry fat[sb.FATEntryCount];
    for (int i = 0; i < sb.FATEntryCount; i++) {
        fat[i].next = -1;
    }
    if (write(vs_fd, fat, sizeof(fat)) != sizeof(fat)) {
        perror("Error writing FAT to virtual disk");
        return -1;
    }

    // Initialize and write the root directory to the virtual disk
    struct directoryEntry root[128];
    for (int i = 0; i < 128; i++) {
        root[i].filename[0] = '\0';
        root[i].size = 0;
        root[i].firstBlock = -1;
    }
    if (write(vs_fd, root, sizeof(root)) != sizeof(root)) {
        perror("Error writing root directory to virtual disk");
        return -1;
    }

    // Close the virtual disk file
    if (close(vs_fd) == -1) {
        perror("Error closing virtual disk file");
        return -1;
    }

    return 0;
}


// this function is partially implemented.
int  vsmount (char *vdiskname)
{
    // Open the virtual disk file
    vs_fd = open(vdiskname, O_RDWR);
    if (vs_fd == -1) {
        printf("Error: Unable to open virtual disk file %s\n", vdiskname);
        return -1;
    }

    // Load the superblock from disk into memory
    if (read(vs_fd, &sb, sizeof(sb)) != sizeof(sb)) {
        printf("Error: Unable to read superblock from disk\n");
        return -1;
    }

    // Allocate memory for the FAT and read it from the disk
    fat = malloc(sb.FATEntryCount * sizeof(*fat));
    if (fat == NULL) {
        printf("Error: Unable to allocate memory for FAT\n");
        return -1;
    }
    if (pread(vs_fd, fat, sb.FATEntryCount * sizeof(*fat), sb.blockSize) != sb.FATEntryCount * sizeof(*fat)) {
        printf("Error: Unable to read FAT from disk\n");
        return -1;
    }

    // Allocate memory for the root directory and read it from the disk
    root = malloc(sb.directoryEntryCount * sizeof(*root));
    if (root == NULL) {
        printf("Error: Unable to allocate memory for root directory\n");
        return -1;
    }
    if (pread(vs_fd, root, sb.directoryEntryCount * sizeof(*root), sb.blockSize * (sb.firstFreeFATEntry + 1)) != sb.directoryEntryCount * sizeof(*root)) {
        printf("Error: Unable to read root directory from disk\n");
        return -1;
    }

    return 0;
}


// this function is partially implemented.
int vsumount(){
    // Write the superblock from memory back to disk
    if (write(vs_fd, &sb, sizeof(sb)) != sizeof(sb)) {
        printf("Error: Unable to write superblock to disk\n");
        return -1;
    }

    // Write the FAT from memory back to disk
    if (pwrite(vs_fd, fat, sb.FATEntryCount * sizeof(*fat), sb.blockSize) != sb.FATEntryCount * sizeof(*fat)) {
        printf("Error: Unable to write FAT to disk\n");
        return -1;
    }

    // Write the root directory from memory back to disk
    if (pwrite(vs_fd, root, sb.directoryEntryCount * sizeof(*root), sb.blockSize * (sb.firstFreeFATEntry + 1)) != sb.directoryEntryCount * sizeof(*root)) {
        printf("Error: Unable to write root directory to disk\n");
        return -1;
    }

    // Free the memory allocated for the FAT and root directory
    free(fat);
    free(root);

    // Close the virtual disk file
    if (close(vs_fd) == -1) {
        printf("Error: Unable to close virtual disk file\n");
        return -1;
    }

    return 0;
}

int vscreate(char *filename, int size){
    // Check if file already exists
    for (int i = 0; i < sb.directoryEntryCount; i++) {
        if (strcmp(root[i].filename, filename) == 0) {
            printf("Error: File %s already exists\n", filename);
            return -1;
        }
    }

    // Check if there is space for a new directory entry
    if (sb.directoryEntryCount == 128) {
        printf("Error: No space for new directory entry\n");
        return -1;
    }

    // Create a new directory entry
    strncpy(root[sb.directoryEntryCount].filename, filename, 31);
    root[sb.directoryEntryCount].size = 0;
    root[sb.directoryEntryCount].firstBlock = -1;
    sb.directoryEntryCount++;

    return 0;
}

int vsopen(char *filename, int mode) {
    // Check if file exists
    int directoryEntry = -1;
    for (int i = 0; i < sb.directoryEntryCount; i++) {
        if (strcmp(root[i].filename, filename) == 0) {
            directoryEntry = i;
            break;
        }
    }

    // Check if file was found
    if (directoryEntry == -1) {
        printf("Error: File %s not found\n", filename);
        return -1;
    }

    // Find a free open file entry
    int freeEntry = -1;
    for (int i = 0; i < MAX_OPEN_FILES; i++) {
        if (openFiles[i].filename[0] == '\0') { // Empty entry
            freeEntry = i;
            break;
        }
    }

    // Check if a free entry was found
    if (freeEntry == -1) {
        printf("Error: No free open file entries\n");
        return -1;
    }

    // Open the file
    strncpy(openFiles[freeEntry].filename, filename, 31);
    openFiles[freeEntry].mode = mode;
    openFiles[freeEntry].position = 0;

    return freeEntry;
}

int vsclose(int fd){
    // Check if file descriptor is valid
    if (fd < 0 || fd >= MAX_OPEN_FILES || openFiles[fd].filename[0] == '\0') {
        printf("Error: Invalid file descriptor\n");
        return -1;
    }

    // Close the file
    openFiles[fd].filename[0] = '\0';
    openFiles[fd].mode = 0;
    openFiles[fd].position = 0;

    return 0;
}

int vssize (int  fd)
{
    // Check if file descriptor is valid
    if (fd < 0 || fd >= MAX_OPEN_FILES || openFiles[fd].filename[0] == '\0') {
        printf("Error: Invalid file descriptor\n");
        return -1;
    }

    // Find the directory entry for the file
    int directoryEntry = -1;
    for (int i = 0; i < sb.directoryEntryCount; i++) {
        if (strcmp(root[i].filename, openFiles[fd].filename) == 0) {
            directoryEntry = i;
            break;
        }
    }

    // Check if file was found
    if (directoryEntry == -1) {
        printf("Error: File not found\n");
        return -1;
    }

    // Return the size of the file
    return root[directoryEntry].size;
}

int vsread(int fd, void *buf, int n){
    // Check if file descriptor is valid
    if (fd < 0 || fd >= MAX_OPEN_FILES || openFiles[fd].filename[0] == '\0') {
        printf("Error: Invalid file descriptor\n");
        return -1;
    }

    // Find the directory entry for the file
    int directoryEntry = -1;
    for (int i = 0; i < sb.directoryEntryCount; i++) {
        if (strcmp(root[i].filename, openFiles[fd].filename) == 0) {
            directoryEntry = i;
            break;
        }
    }

    // Check if file was found
    if (directoryEntry == -1) {
        printf("Error: File not found\n");
        return -1;
    }

    // Check if the file is open in read mode
    if (openFiles[fd].mode != 0) {
        printf("Error: File not open in read mode\n");
        return -1;
    }

    // Read the data from the file
    int bytesRead = 0;
    int block = root[directoryEntry].firstBlock;
    char blockData[sb.blockSize];
    while (bytesRead < n && block != -1) {
        read_block(block, blockData);
        int bytesToRead = min(n - bytesRead, sb.blockSize - openFiles[fd].position);
        memcpy(buf + bytesRead, blockData + openFiles[fd].position, bytesToRead);
        bytesRead += bytesToRead;
        openFiles[fd].position += bytesToRead;
        if (openFiles[fd].position == sb.blockSize) {
            openFiles[fd].position = 0;
            block = fat[block];
        }
    }

    return bytesRead;
}


int vsappend(int fd, void *buf, int n)
{
    // Check if file descriptor is valid
    if (fd < 0 || fd >= MAX_OPEN_FILES || openFiles[fd].filename[0] == '\0') {
        printf("Error: Invalid file descriptor\n");
        return -1;
    }

    // Find the directory entry for the file
    int directoryEntry = -1;
    for (int i = 0; i < sb.directoryEntryCount; i++) {
        if (strcmp(root[i].filename, openFiles[fd].filename) == 0) {
            directoryEntry = i;
            break;
        }
    }

    // Check if file was found
    if (directoryEntry == -1) {
        printf("Error: File not found\n");
        return -1;
    }

    // Check if the file is open in write mode
    if (openFiles[fd].mode != 1) {
        printf("Error: File not open in write mode\n");
        return -1;
    }

    // Append the data to the file
    int bytesWritten = 0;
    int block = root[directoryEntry].firstBlock;
    char blockData[sb.blockSize];
    while (bytesWritten < n) {
        if (block == -1 || openFiles[fd].position == sb.blockSize) {
            int newBlock = find_free_block();
            if (newBlock == -1) {
                printf("Error: No free blocks\n");
                break;
            }
            if (block != -1) {
                fat[block] = newBlock;
            } else {
                root[directoryEntry].firstBlock = newBlock;
            }
            block = newBlock;
            openFiles[fd].position = 0;
        }
        int bytesToWrite = min(n - bytesWritten, sb.blockSize - openFiles[fd].position);
        memcpy(blockData + openFiles[fd].position, buf + bytesWritten, bytesToWrite);
        write_block(block, blockData);
        bytesWritten += bytesToWrite;
        openFiles[fd].position += bytesToWrite;
    }

    root[directoryEntry].size += bytesWritten;
    return bytesWritten;
}

int vsdelete(char *filename)
{
    // Find the directory entry for the file
    int directoryEntry = -1;
    for (int i = 0; i < sb.directoryEntryCount; i++) {
        if (strcmp(root[i].filename, filename) == 0) {
            directoryEntry = i;
            break;
        }
    }

    // Check if file was found
    if (directoryEntry == -1) {
        printf("Error: File not found\n");
        return -1;
    }

    // Check if the file is open
    for (int i = 0; i < MAX_OPEN_FILES; i++) {
        if (strcmp(openFiles[i].filename, filename) == 0) {
            printf("Error: File is open\n");
            return -1;
        }
    }

    // Free the blocks used by the file
    int block = root[directoryEntry].firstBlock;
    while (block != -1) {
        int nextBlock = fat[block];
        fat[block] = -1;
        block = nextBlock;
    }

    // Remove the directory entry for the file
    root[directoryEntry].filename[0] = '\0';
    root[directoryEntry].size = 0;
    root[directoryEntry].firstBlock = -1;

    return 0;
}

