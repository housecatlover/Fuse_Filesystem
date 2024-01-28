// based on cs3650 starter code
#include "storage.h"
#include "directory.h"
#include "bitmap.h"

#include <assert.h>
#include <bsd/string.h>
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdlib.h>

#define FUSE_USE_VERSION 26
#include <fuse.h>
#define CHECK_ENTRY \
  if (entry == 0)   \
  {                 \
    errno = ENOENT; \
    return -1;      \
  }

// mode_t DIRECTORY_MODE = 040755;
mode_t FILE_MODE = 0100644;
mode_t FILE_MASK = 0100000;

// implementation for: man 2 access
// Checks if a file exists.
int nufs_access(const char *path, int mask)
{
  dirent_t *entry = directory_path_lookup(path);
  int rv;
  if (mask == F_OK)
  { //R/W/X
    rv = entry == 0 ? -ENOENT : 0;
  }
  else
  {
    //I was wrong about how masks worked so this just lets everything pass
    rv = 0;
  }
  printf("access(%s, %04o) -> %d\n", path, mask, rv);
  return rv;
}

// Gets an object's attributes (type, permissions, size, etc).
// Implementation for: man 2 stat
// This is a crucial function.
int nufs_getattr(const char *path, struct stat *st)
{
  printf("getting the attributes of %s\n", path);
  if (strcmp(path, ".") == 0)
  { //a bit of default case
    st->st_mode = FILE_MODE;
    st->st_size = get_inode(((dirent_t *)get_root_entry())->inum)->size;
    st->st_uid = getuid();
    return 0;
  }

  dirent_t *entry = directory_path_lookup(path);
  if (entry == 0)
  {
    printf("entry does not exist\n");
    return -ENOENT;
  }
  inode_t *node = get_inode(entry->inum);

  st->st_mode = entry->mode;
  st->st_size = node->size;
  st->st_uid = getuid();

  printf("getattr(%s) -> (%d) {mode: %04o, size: %ld}\n", path, 0, st->st_mode,
         st->st_size);
  return 0;
}

// implementation for: man 2 readdir
// lists the contents of a directory
int nufs_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
                 off_t offset, struct fuse_file_info *fi)
{
  printf("listing directory contents of %s\n", path);
  struct stat st;
  int rv;

  filler(buf, ".", &st, 0);

  rv = nufs_getattr("/", &st);
  assert(rv == 0);

  slist_t *files = directory_list(path);
  if (files != 0)
  { //if there are any files
    slist_t *node = files;
    char *full_name;
    while (node != 0)
    {
      full_name = (char *)malloc(strlen(path) + strlen(node->data) + 2); // \0 and /
      strcpy(full_name, path);
      if (strcmp(path, ROOT_NAME) != 0)
      {
        strcat(full_name, "/");
      }
      strcat(full_name, node->data);
      printf("full_name = %s\n", full_name);

      rv = nufs_getattr(full_name, &st);
      assert(rv == 0);
      filler(buf, node->data, &st, 0);
      node = node->next;
      free(full_name);
    }
    slist_free(files);
  }

  printf("readdir(%s) -> %d\n", path, rv);
  return rv;
}

// mknod makes a filesystem object like a file or directory
// called for: man 2 open, man 2 link
// Note, for this assignment, you can alternatively implement the create
// function.
int nufs_mknod(const char *path, mode_t mode, dev_t rdev)
{
  printf("Given mode = %d, given rdev = %ld\n", mode, rdev);
  char *location = (char *)malloc(strlen(path) + 1);
  copy_folder(path, location, strlen(path));

  dirent_t *folder = directory_path_lookup(location);
  free(location);
  char name[DIR_NAME_LENGTH];
  copy_file(path, name, strlen(path));

  int inum = alloc_inode();
  if (inum == -1)
  {
    errno = EDQUOT;
    return -1;
  }
  printf("alloced node sucesffuly\n");

  if (rdev == FILE_MASK) //just passed from mkdir
  {
    printf("initalizing it as a directory\n");
    directory_const(get_inode(inum));
  }

  directory_put(get_inode(folder->inum), name, inum, mode); //pre directory this is just ROOT_NODE, name, node
  printf("put successful\n");
  int rv = 0;
  printf("mknod(%s, %04o) -> %d\n", path, mode, rv);
  return rv;
}

// most of the following callbacks implement
// another system call; see section 2 of the manual
int nufs_mkdir(const char *path, mode_t mode)
{
  int rv = nufs_mknod(path, mode | 040000, FILE_MASK);
  printf("mkdir(%s) -> %d\n", path, rv);
  return rv;
}

int nufs_unlink(const char *path)
{
  dirent_t *entry = directory_path_lookup(path);
  CHECK_ENTRY
  free_inode(entry->inum);

  char *source = (char *)malloc(strlen(path) + 1);
  copy_folder(path, source, strlen(path));
  entry = directory_path_lookup(source);
  free(source);
  CHECK_ENTRY
  char file[DIR_NAME_LENGTH];
  copy_file(path, file, strlen(path));
  directory_delete(get_inode(entry->inum), file);

  int rv = 0;
  printf("unlink(%s) -> %d\n", path, rv);
  return rv;
}

int nufs_link(const char *from, const char *to)
{
  char *source = (char *)malloc(strlen(from) + 1);
  copy_folder(from, source, strlen(from));
  dirent_t *entry = directory_path_lookup(source);
  free(source);
  CHECK_ENTRY
  dirent_t *prev_entry = entry;

  char *dest = (char *)malloc(strlen(to) + 1);
  copy_folder(to, dest, strlen(to));
  entry = directory_path_lookup(dest);
  free(dest);
  CHECK_ENTRY

  free_inode(prev_entry->inum);
  prev_entry->inum = entry->inum;
  get_inode(entry->inum)->refs++;

  int rv = 0;
  printf("link(%s => %s) -> %d\n", from, to, rv);
  return rv;
}

int nufs_rmdir(const char *path)
{
  dirent_t *entry = directory_path_lookup(path);
  CHECK_ENTRY
  inode_t *node = get_inode(entry->inum);
  int size = node->size;

  for (int i = 0; i < size / BLOCK_SIZE; ++i)
  {
    header_t *header = (header_t *)blocks_get_block(inode_get_bnum(node, i));
    for (int j = 1; j < DIR_PER_BLOCK; j++)
    {
      if (bitmap_get(header->bm, j) == 1)
      {
        errno = ENOTEMPTY;
        printf("rmdir(%s) -> %d\n", path, -1);
        return -1;
      }
    }
  }

  char *folder = (char *)malloc(strlen(path) + 1);
  copy_folder(path, folder, strlen(path));
  dirent_t *super_folder = directory_path_lookup(folder);
  copy_file(path, folder, strlen(path));
  directory_delete(get_inode(super_folder->inum), folder);
  free(folder);

  int rv = 0;
  printf("rmdir(%s) -> %d\n", path, rv);
  return rv;
}

// implements: man 2 rename
// called to move a file within the same filesystem
int nufs_rename(const char *from, const char *to)
{
  printf("getting source location ");
  char *source = (char *)malloc(strlen(source) + 1);
  copy_folder(from, source, strlen(from));
  printf("%s\n", source);
  dirent_t *entry = directory_path_lookup(source);
  if (entry == 0)
  {
    free(source);
    errno = ENOENT;
    return -1;
  }
  dirent_t *prev_entry = entry;

  printf("getting destination location");
  char *dest = (char *)malloc(strlen(to) + 1);
  copy_folder(to, dest, strlen(to));
  printf("%s\n", dest);
  entry = directory_path_lookup(dest);
  if (entry == 0)
  {
    free(source);
    free(dest);
    errno = ENOENT;
    return -1;
  }
  int rv;
  copy_file(to, dest, strlen(to));
  printf("new file name %s\n", dest);
  rv = directory_put(get_inode(entry->inum), dest, prev_entry->inum, prev_entry->mode);
  printf("if 0 == %d succesfully put a new dirent_t in the new location\n", rv);
  if (rv == 0)
  {
    copy_file(from, source, strlen(from));
    rv = directory_delete(get_inode(prev_entry->inum), source);
    printf("if 0 == %d succesfully removed if from its old location", rv);
  } //else could delete the duplicate
  free(source);
  free(dest);

  rv = 0;
  printf("rename(%s => %s) -> %d\n", from, to, rv);
  return rv;
}

int nufs_chmod(const char *path, mode_t mode)
{
  dirent_t *entry = directory_path_lookup(path);
  if (entry)
    entry->mode = mode;
  int rv = -1;
  printf("chmod(%s, %04o) -> %d\n", path, mode, rv);
  return rv;
}

int nufs_truncate(const char *path, off_t size)
{
  dirent_t *entry = directory_path_lookup(path);
  CHECK_ENTRY
  int inum = entry->inum;
  inode_t *node = get_inode(inum);
  int initial = node->size;
  shrink_inode(node, size);
  grow_inode(node, size);

  int rv = 0;
  printf("truncate(%s, %ld bytes) -> %d\n", path, size, rv);
  return rv;
}

// This is called on open, but doesn't need to do much
// since FUSE doesn't assume you maintain state for
// open files.
// You can just check whether the file is accessible.
int nufs_open(const char *path, struct fuse_file_info *fi)
{
  dirent_t *entry = directory_path_lookup(path);
  CHECK_ENTRY
  int inum = entry->inum;
  int rv = inum == -1 ? 1 : 0;
  printf("open(%s) -> %d\n", path, rv);
  return rv;
}

// Actually read data
int nufs_read(const char *path, char *buf, size_t size, off_t offset,
              struct fuse_file_info *fi)
{
  dirent_t *entry = directory_path_lookup(path);
  CHECK_ENTRY
  int inum = entry->inum;
  int rv = inode_read(get_inode(inum), buf, size, offset);
  printf("reading(%s, %ld bytes, @+%ld) -> %d\n", path, size, offset, rv);
  return rv;
}

// Actually write data
int nufs_write(const char *path, const char *buf, size_t size, off_t offset,
               struct fuse_file_info *fi)
{
  dirent_t *entry = directory_path_lookup(path);
  CHECK_ENTRY
  int inum = entry->inum;
  int rv = inode_write(get_inode(inum), buf, size, offset);

  printf("write(%s, %ld bytes, @+%ld) -> %d\n", path, size, offset, rv);
  return rv;
}

// Update the timestamps on a file or directory.
int nufs_utimens(const char *path, const struct timespec ts[2])
{
  int rv = -1;
  printf("utimens(%s, [%ld, %ld; %ld %ld]) -> %d\n", path, ts[0].tv_sec,
         ts[0].tv_nsec, ts[1].tv_sec, ts[1].tv_nsec, rv);
  return rv;
}

// Extended operations
int nufs_ioctl(const char *path, int cmd, void *arg, struct fuse_file_info *fi,
               unsigned int flags, void *data)
{
  int rv = -1;
  printf("ioctl(%s, %d, ...) -> %d\n", path, cmd, rv);
  return rv;
}

void nufs_init_ops(struct fuse_operations *ops)
{
  memset(ops, 0, sizeof(struct fuse_operations));
  ops->access = nufs_access;
  ops->getattr = nufs_getattr;
  ops->readdir = nufs_readdir;
  ops->mknod = nufs_mknod;
  // ops->create   = nufs_create; // alternative to mknod
  ops->mkdir = nufs_mkdir;
  ops->link = nufs_link;
  ops->unlink = nufs_unlink;
  ops->rmdir = nufs_rmdir;
  ops->rename = nufs_rename;
  ops->chmod = nufs_chmod;
  ops->truncate = nufs_truncate;
  ops->open = nufs_open;
  ops->read = nufs_read;
  ops->write = nufs_write;
  ops->utimens = nufs_utimens;
  ops->ioctl = nufs_ioctl;
};

struct fuse_operations nufs_ops;

int main(int argc, char *argv[])
{
  assert(argc > 2 && argc < 6);
  storage_init(argv[--argc]);
  nufs_init_ops(&nufs_ops);

  assert(sizeof(dirent_t) == sizeof(header_t));
  return fuse_main(argc, argv, &nufs_ops, NULL);
}
