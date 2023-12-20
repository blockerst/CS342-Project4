#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include "vsfs.h"

#define MAX_OPEN_FILES 16
#define MIN(a, b) (((a) < (b)) ? (a) : (b))

//From 0 superblock
struct superblock {
    int blockSize;
    int blockCount;
    int padding[2012];
    int FATEntryCount;
    int firstFreeBlock;
    int freeBlockCount;
    int firstFreeFATEntry;
    int reservedBlockCount;
    int directoryEntryCount;
    int firstFreeDirectoryEntry;
};

//From 1 to 32
struct FATentry{
    int next;
};

//From 33 to 41
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

int vsformat(char *vdiskname, unsigned int m)
{
    char command[1000];
    int size;
    int num = 1;
    int count;
    size = num << m;
    count = size / BLOCKSIZE;

    sprintf (command, "dd if=/dev/zero of=%s bs=%d count=%d",vdiskname, BLOCKSIZE, count);
    //printf ("executing command = %s\n", command);
    system (command);

    // Open the virtual disk file
    vs_fd = open(vdiskname, O_RDWR | O_CREAT, S_IRUSR | S_IWUSR);
    if (vs_fd == -1)
    {
        perror("Error opening/creating virtual disk");
        return -1;
    }

    // Set the file size to the desired size
    if (ftruncate(vs_fd, size) == -1)
    {
        perror("Error truncating virtual disk");
        close(vs_fd);
        return -1;
    }

    // Allocate memory for superblock, FAT, and root directory
    fat = malloc(count * sizeof(*fat));
    root = malloc(128 * sizeof(*root)); // Assuming 128 entries in the root directory

    // Initialize the superblock and other structures
    sb.blockSize = BLOCKSIZE;
    sb.blockCount = count;
    sb.freeBlockCount = count - 1; // One block is reserved for superblock
    sb.reservedBlockCount = 1;
    sb.firstFreeBlock = 1;
    sb.directoryEntryCount = 0;
    sb.FATEntryCount = count;
    sb.firstFreeDirectoryEntry = 0;
    sb.firstFreeFATEntry = 1;

    // Initialize the FAT table
    for (int i = 0; i < count; i++)
    {
        fat[i].next = -1;
    }

    // Initialize the root directory
    for (int i = 0; i < 128; i++)
    {
        root[i].filename[0] = '\0';
        root[i].size = 0;
        root[i].firstBlock = -1;
    }

    // Write the superblock to the virtual disk file
    if (write_block(&sb, 0) == -1)
    {
        perror("Error writing superblock to virtual disk");
        free(fat);
        free(root);
        close(vs_fd);
        return -1;
    }

    // Write the FAT table to the virtual disk file 32 times
    for (int i = 0; i < 32; i++)
    {
        if (write_block(fat, i + 1) == -1)
        {
            perror("Error writing FAT to virtual disk");
            free(fat);
            free(root);
            close(vs_fd);
            return -1;
        }
    }
    
    //33-41 root directory
    // Write the root directory to the virtual disk file
    for (int i = 0; i < 9; i++){
        if (write_block(root, i + 33) == -1)
        {
            perror("Error writing root directory to virtual disk");
            free(fat);
            free(root);
            close(vs_fd);
            return -1;
        }
    }

    // Close the virtual disk file
    close(vs_fd);

    // Free memory
    free(fat);
    free(root);

    return 0;
}

int vsmount(char *vdiskname)
{
    // Get the file descriptor of the virtual disk
    vs_fd = open(vdiskname, O_RDWR);
    if (vs_fd == -1)
    {
        perror("Error opening virtual disk");
        return -1;
    }

    // Read the superblock from the virtual disk file
    if (read_block(&sb, 0) == -1)
    {
        perror("Error reading superblock from virtual disk");
        close(vs_fd);
        return -1;
    }

    //Allocate memory for superblock, FAT, and root directory
    fat = malloc(sb.FATEntryCount * sizeof(*fat));
    root = malloc(sb.directoryEntryCount * sizeof(*root));
    if (fat == NULL || root == NULL) {
        perror("Error allocating memory");
        close(vs_fd);
        return -1;
    }

    // Read the FAT table from the virtual disk file
    for (int i = 0; i < 32; i++)
    {
        if (read_block(fat, i + 1) == -1)
        {
            perror("Error reading FAT from virtual disk");
            free(fat);
            free(root);
            close(vs_fd);
            return -1;
        }
    }
    
    // Read the root directory from the virtual disk file
    for(int i = 0; i < 9; i++){
        if (read_block(root, i + 33) == -1)
        {
            perror("Error reading root directory from virtual disk");
            free(fat);
            free(root);
            close(vs_fd);
            return -1;
        }
    }

    // Initialize the open file table
    for (int i = 0; i < MAX_OPEN_FILES; i++)
    {
        openFiles[i].filename[0] = '\0';
        openFiles[i].mode = MODE_READ;
        openFiles[i].position = 0;
    }

    return 0;
}

// this function is partially implemented.
int vsumount(){
    // write superblock to virtual disk file using write_block()
    if (write_block(&sb, 0) == -1)
    {
        perror("Error writing superblock to virtual disk");
        free(fat);
        free(root);
        close(vs_fd);
        return -1;
    }

    // write FAT to virtual disk file using write_block()
    for (int i = 0; i < 32; i++)
    {
        if (write_block(fat, i + 1) == -1)
        {
            perror("Error writing FAT to virtual disk");
            free(fat);
            free(root);
            close(vs_fd);
            return -1;
        }
    }

    // write root directory to virtual disk file using write_block()
    for(int i = 0; i < 9; i++){
        if (write_block(root, i + 33) == -1)
        {
            perror("Error writing root directory to virtual disk");
            free(fat);
            free(root);
            close(vs_fd);
            return -1;
        }
    }
    
    fsync (vs_fd); // synchronize kernel file cache with the disk
    close (vs_fd);

    // free memory
    free(fat);
    free(root);
    return (0);
}

int vscreate(char *filename){
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

    // Check if the data can be read from the file
    if (openFiles[fd].mode != 0) {
        printf("Error: File not open in read mode\n");
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

    // Read the data from the file
    int bytesRead = 0;
    int block_num = root[directoryEntry].firstBlock;
    while (bytesRead < n && block_num != -1) {
        // Calculate the block number and offset
        int offset = openFiles[fd].position % BLOCKSIZE;

        // Read the data from the block
        char block_data[BLOCKSIZE];
        read_block(block_data, block_num);

        // Calculate the number of bytes to read
        int bytes_to_read = MIN(BLOCKSIZE - offset, n - bytesRead);

        // Copy the data to the buffer
        memcpy(buf + bytesRead, block_data + offset, bytes_to_read);

        // Update the file position and the number of bytes read
        openFiles[fd].position += bytes_to_read;
        bytesRead += bytes_to_read;

        // Move to the next block
        block_num = fat[block_num].next;
    }

    return bytesRead;
}

//write to the end) new data to the file. The parameter fd is the
//file descriptor. The parameter buf is pointing to (i.e., is the address of) a static
//array holding the data or a dynamically allocated memory space holding the
//data. The parameter n is the size of the data to write (append) into the file. In
//case of an error, the function returns −1. Otherwise, it returns the number of
//bytes successfully appended.
int vsappend(int fd, void *buf, int n)
{
    // Check if file descriptor is valid
    if (fd < 0 || fd >= MAX_OPEN_FILES || openFiles[fd].filename[0] == '\0') {
        printf("Error: Invalid file descriptor\n");
        return -1;
    }

    // Check if the file is open in append mode
    if (openFiles[fd].mode != MODE_APPEND) {
        printf("Error: File not open in append mode\n");
        return -1;
    }

    // Calculate the number of blocks needed
    int blocks_needed = (n + BLOCKSIZE - 1) / BLOCKSIZE;

    // Check if there are enough free blocks
    if (blocks_needed > sb.freeBlockCount) {
        printf("Error: Not enough free space\n");
        return -1;
    }

    // Write the data to the file
    int bytes_written = 0;
    while (bytes_written < n) {
        // Calculate the block number and offset
        int block_num = openFiles[fd].position / BLOCKSIZE + 3; // +3 to account for sb, fat, and root
        int offset = openFiles[fd].position % BLOCKSIZE;

        // If we're at the start of a block, we need to allocate a new block
        if (offset == 0) {
            int new_block = sb.firstFreeBlock;
            sb.firstFreeBlock = fat[new_block].next;
            fat[new_block].next = -1;
            
            block_num = new_block;
        }

        // Write the data to the block
        int bytes_to_write = MIN(BLOCKSIZE - offset, n - bytes_written);

        // Read the current block data
        char block_data[BLOCKSIZE];
        read_block(block_data, block_num);

        // Append the new data to the block data
        memcpy(block_data + offset, buf + bytes_written, bytes_to_write);

        // Write the updated block data back to the block
        write_block(block_data, block_num);

        // Update the file position and the number of bytes written
        openFiles[fd].position += bytes_to_write;
        bytes_written += bytes_to_write;
    }

    return bytes_written;
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
        int nextBlock = fat[block].next;
        fat[block].next = -1;
        block = nextBlock;
    }

    // Remove the directory entry for the file
    root[directoryEntry].filename[0] = '\0';
    root[directoryEntry].size = 0;
    root[directoryEntry].firstBlock = -1;

    return 0;
}