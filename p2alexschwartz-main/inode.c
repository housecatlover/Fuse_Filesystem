#include "inode.h"
#include "bitmap.h"

#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <assert.h>
#include <math.h>

void print_inode(inode_t *node)
{
    printf("refs: %d, mode: %d, size: %d, block 0: %d, block 1: %d, block 2: %d, continuing blocks %d\n",
           node->refs, node->mode, node->size, node->blocks[0], node->blocks[1], node->blocks[2], node->cont_block);
}

inode_t *get_inode(int inum)
{
    assert(inum >= 0 && inum < 256);
    void *block = blocks_get_block(inum / 128 + 1);
    return block + (inum % 128) * sizeof(inode_t);
}

int alloc_inode()
{
    void *bbm = get_inode_bitmap();

    for (int ii = 1; ii < BLOCK_COUNT; ++ii)
    { //equal number of blocks and inodes so it never runs out
        if (!bitmap_get(bbm, ii))
        {
            bitmap_put(bbm, ii, 1);
            printf("+ alloc_inode() -> %d\n", ii);
            inode_t *node = get_inode(ii);
            node->cont_block = 0;
            node->refs = 1;
            return ii;
        }
    }

    return -1;
}

void free_inode(int inum)
{
    inode_t *node = get_inode(inum);
    if (--node->refs == 0)
    {
        shrink_inode(node, 0); //remove all the blocks
        void *bbm = get_inode_bitmap();
        bitmap_put(bbm, inum, 0);
        printf(" + free_inode(%d)\n", inum);
    }
}

int round_up(double x)
{ //it wasn't recognizing ceil
    int truncated = (int)x;
    return x > truncated ? x + 1 : x;
}

int grow_inode(inode_t *node, int size)
{
    if (node->size >= size)
    {
        printf("large enough already\n");
        return node->size;
    }
    int current = round_up((double)node->size / BLOCK_SIZE); //number of blocks
    printf("Starts with %d blocks\n", current);
    int new_block;
    for (int i = current; i < (float)size / BLOCK_SIZE; ++i)
    {
        if (i < DIRECT_BLOCKS)
        {
            printf("allocating a new direct block\n");
            new_block = alloc_block();
            if (new_block == -1)
                return i * BLOCK_SIZE; //how much space was succesfully allocated
            node->blocks[i] = new_block;
        }
        else
        {
            if (node->cont_block == 0)
            {
                printf("allocating the cont_block\n");
                new_block = alloc_block();
                if (new_block == -1)
                    return i * BLOCK_SIZE; //how much space was succesfully allocated
                node->cont_block = new_block;
            }
            printf("allocating a new block inside cont_block\n");
            new_block = alloc_block();
            if (new_block == -1)
                return i * BLOCK_SIZE; //how much space was succesfully allocated
            ((int *)blocks_get_block(node->cont_block))[i - DIRECT_BLOCKS] = new_block;
        }
    }
    node->size = size;
    return size;
}

//0 clears
int shrink_inode(inode_t *node, int size)
{
    if (node->size <= size)
    {
        return node->size;
    }
    int current = node->size / BLOCK_SIZE;
    for (int i = current; i * BLOCK_SIZE >= size; --i)
    {
        if (i < DIRECT_BLOCKS)
        {
            free_block(node->blocks[i]);
        }
        else
        {
            free_block(((int *)blocks_get_block(node->cont_block))[i - DIRECT_BLOCKS]);
            if (i == DIRECT_BLOCKS)
            {
                node->cont_block = 0;
            }
        }
    }
    node->size = size;
    return size;
}

// Returns the real block number pointed to by the given node's file_bnum th pointer
int inode_get_bnum(inode_t *node, int file_bnum)
{
    if (file_bnum < DIRECT_BLOCKS)
    {
        return node->blocks[file_bnum];
    }
    if (node->cont_block == 0)
    {
        return -1;
    }
    return ((int *)blocks_get_block(node->cont_block))[file_bnum - DIRECT_BLOCKS];
}

// The node to write to, the data, the size of the data, the offset into the node to start writing
int inode_write(inode_t *node, const void *buf, size_t size, off_t offset)
{
    assert(offset <= node->size);
    int end_size = size + offset;
    if (end_size > node->size)
    { //if the number of blocks is the same grow_inode will do nothing
        printf("growing node to size %d\n", end_size);
        grow_inode(node, end_size);
    }
    int position = offset % BLOCK_SIZE;    //position to start writing from
    int remaining = BLOCK_SIZE - position; //the amount of data that goes in the current block
    if (size < remaining)
    { //or the total amount of data, whichever is smaller
        remaining = size;
    }
    int index = 0;                   //the position in the data
    int block = offset / BLOCK_SIZE; //which inode relative block to be writing to
    print_inode(node);

    while (index < size)
    {
        printf("copying %d bytes from position %d to position %d in block %d\n",
               remaining, index, position + index, block);
        int bnum = inode_get_bnum(node, 0);
        printf("writing to block num %d at %p\n", bnum, blocks_get_block(bnum));
        memcpy(blocks_get_block(inode_get_bnum(node, block)) + position + index, buf + index, remaining);
        index += remaining; //increment by the amount written
        block++;            //go to the next block
        position = 0;
        if (BLOCK_SIZE < size - index)
        { //if theres at least an entire block of data left
            remaining = BLOCK_SIZE;
        }
        else
        { //the rest of the data
            remaining = size - index;
        }
    }
    node->size = index; //redundent on success
    return index;
}

// The node to read from, where to put the data, the amount of the data, the offset into the node to start reading
int inode_read(inode_t *node, void *buf, size_t size, off_t offset)
{
    if (offset + size > node->size)
    {
        printf("Tried to read %ld bytes from a node with %d stored (offset by %ld)\n", size, node->size, offset);
        size = node->size - offset;
    }

    int remaining = BLOCK_SIZE - offset % BLOCK_SIZE;
    if (size < remaining)
    {
        remaining = size;
    }
    int index = 0;
    int block = offset / BLOCK_SIZE;
    while (index < size)
    {
        printf("copying over %d bytes from the %dth block of the given inode to the given buffer\n", remaining, block);
        memcpy(buf + index, blocks_get_block(inode_get_bnum(node, block)), remaining);
        index += remaining;
        block++;
        if (BLOCK_SIZE < size - index)
        {
            remaining = BLOCK_SIZE;
        }
        else
        {
            remaining = size - index;
        }
    }
    return index;
}