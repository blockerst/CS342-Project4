#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include "vsfs.h"

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

// this function is partially implemented.
int vsformat (char *vdiskname, unsigned int m)
{
    char command[1000];
    int size;
    int num = 1;
    int count;
    size  = num << m;
    count = size / BLOCKSIZE;
    //    printf ("%d %d", m, size);
    sprintf (command, "dd if=/dev/zero of=%s bs=%d count=%d",
             vdiskname, BLOCKSIZE, count);
    //printf ("executing command = %s\n", command);
    system (command);

    // now write the code to format the disk.
    // .. your code...
    
    return (0); 
}


// this function is partially implemented.
int  vsmount (char *vdiskname)
{
    // open the Linux file vdiskname and in this
    // way make it ready to be used for other operations.
    // vs_fd is global; hence other function can use it. 
    vs_fd = open(vdiskname, O_RDWR);
    // load (chache) the superblock info from disk (Linux file) into memory
    // load the FAT table from disk into memory
    // load root directory from disk into memory
    //...
    return(0);
}


// this function is partially implemented.
int vsumount ()
{
    // write superblock to virtual disk file
    // write FAT to virtual disk file
    // write root directory to virtual disk file

    fsync (vs_fd); // synchronize kernel file cache with the disk
    close (vs_fd);
    return (0); 
}


int vscreate(char *filename)
{
    return (0);
}


int vsopen(char *file, int mode)
{
    return (0); 
}

int vsclose(int fd){
    return (0); 
}

int vssize (int  fd)
{
    return (0); 
}

int vsread(int fd, void *buf, int n){
    return (0); 
}


int vsappend(int fd, void *buf, int n)
{
    return (0); 
}

int vsdelete(char *filename)
{
    return (0); 
}

