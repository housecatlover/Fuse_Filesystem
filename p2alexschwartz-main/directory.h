// Directory manipulation functions.
//
// Feel free to use as inspiration. Provided as-is.

// Based on cs3650 starter code
#ifndef DIRECTORY_H
#define DIRECTORY_H

#define DIR_NAME_LENGTH 48

#include "inode.h"
#include "slist.h"

#include <sys/types.h>
#include <stdint.h>

typedef struct directent
{
  char name[DIR_NAME_LENGTH + 1]; // hidden space for a null character
  int inum;
  mode_t mode;
  uint8_t flags; //bits 0 is file v directory, read write and execute are preset from there
  char _reserved[2];
} dirent_t;

typedef struct header
{                //The header of a directory, must be the same size as directent
  uint8_t bm[8]; //A bitmap of the used directent indicies
  int free;
  char _reserved[52];
} header_t;

extern const int ROOT_INUM;
extern const char ROOT_NAME[2];
extern const int DIR_PER_BLOCK;

void directory_init();
void directory_const(inode_t *dirent_t);
//get the inum of the directory with the given name in the given directory
dirent_t *directory_lookup(inode_t *di, const char *name);
//get all the files at the given path realitive to the root directory
dirent_t *directory_path_lookup(const char *path);
//get all the files at the given path realitive to the given directory
dirent_t *directory_realitive_path_lookup(inode_t *di, slist_t *path);
int directory_put(inode_t *di, const char *name, int inum, mode_t mode);
int directory_delete(inode_t *di, const char *name);
slist_t *directory_list(const char *path);
//get all the files at the given path realitive to the given directory
slist_t *directory_realitive_list(inode_t *di, slist_t *path);
//returns a list of inums of all the files / directories in a specific directory
slist_t *directory_list_given(inode_t *di);
void print_directory(inode_t *dd);
void copy_folder(const char *path, char *buf, size_t size);
void copy_file(const char *path, char *buf, size_t size);

#endif
