// Inode manipulation routines.
//
// Feel free to use as inspiration. Provided as-is.

// based on cs3650 starter code
#ifndef INODE_H
#define INODE_H
#define DIRECT_BLOCKS 4

#include "blocks.h"

typedef struct inode
{
  int refs;                  // reference count
  int mode;                  // permission & type
  int size;                  // bytes
  int blocks[DIRECT_BLOCKS]; // single block pointer (if max file size <= 4K)
  int cont_block;
} inode_t;

void print_inode(inode_t *node);
inode_t *get_inode(int inum);
int alloc_inode();
void free_inode(int inum);

// Grow the inode's references to the point that it could contain size (rounded up to the nearest block)
int grow_inode(inode_t *node, int size);

// Shrink the inode's references to the point that it could contain size (rounded up to the nearest block)
int shrink_inode(inode_t *node, int size);

// Returns the real block number pointed to by the given node's file_bnum th pointer
int inode_get_bnum(inode_t *node, int file_bnum);

// The node to write to, the data, the size of the data, the offset into the node to start writing
int inode_write(inode_t *node, const void *buf, size_t size, off_t offset);

// The node to read from, where to put the data, the amount of the data, the offset into the node to start reading

int inode_read(inode_t *node, void *buf, size_t size, off_t offset);

#endif
