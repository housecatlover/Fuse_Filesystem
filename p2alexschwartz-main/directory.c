#include "directory.h"
#include "blocks.h"
#include "bitmap.h"

#include <assert.h>
#include <string.h>
#include <stdlib.h>

const int ROOT_INUM = 0;
const char ROOT_NAME[2] = "/";
const int DIR_PER_BLOCK = 64; //BLOCK_SIZE / sizeof(dirent_t)

void directory_init()
{
    // inode 0 stores the root directory
    void *ibm = get_inode_bitmap();
    bitmap_put(ibm, ROOT_INUM, 1);

    inode_t *root = get_inode(ROOT_INUM);
    if (root->size == 0)
    { //if this is the first init
        directory_const(root);
    }

    dirent_t *root_entry = (dirent_t *)get_root_entry();
    strcpy(root_entry->name, ROOT_NAME);
    root_entry->inum = ROOT_INUM;
    root_entry->mode = 040755; //directory default
}

void directory_const(inode_t *di)
{
    printf("Constructing a new directory\n");
    dirent_t directory[DIR_PER_BLOCK];
    header_t *header = (header_t *)directory;
    header->free = DIR_PER_BLOCK - 1;
    bitmap_put(header->bm, 0, 1); //Position 0 is taken as the header
    for (int i = 1; i < DIR_PER_BLOCK; i++)
    { //ensure garbage data isn't in the map
        bitmap_put(header->bm, i, 0);
    }
    assert(BLOCK_SIZE == inode_write(di, directory, BLOCK_SIZE, 0)); //TODO truncate
}

//Get the dirent_t of the named directory / file contained within the given directory
dirent_t *directory_lookup(inode_t *di, const char *name)
{
    dirent_t *directory = (dirent_t *)malloc(di->size);
    assert(di->size == inode_read(di, directory, di->size, 0));
    for (int i = 1; i < di->size / sizeof(dirent_t); ++i)
    {
        header_t *header = (header_t *)(directory + DIR_PER_BLOCK * (i / DIR_PER_BLOCK)); //header of the portion
        if (0 == bitmap_get(header->bm, i % DIR_PER_BLOCK) || i % DIR_PER_BLOCK == 0)
        { //empty or full with the header
            continue;
        }
        char *name_space = directory[i].name;
        if (strcmp(name_space, name) == 0)
        {
            dirent_t *real = (dirent_t *)blocks_get_block(inode_get_bnum(di, i / DIR_PER_BLOCK)); //modifiable
            free(directory);
            return real + i % DIR_PER_BLOCK;
        }
    }
    free(directory);
    return 0;
}

dirent_t *directory_path_lookup(const char *path)
{
    if (strlen(path) == 0 || strcmp(path, ROOT_NAME) == 0)
    {
        return get_root_entry();
    }
    slist_t *folders = slist_explode(path[0] == '/' ? path + 1 : path, '/'); //cut the initial /
    dirent_t *output = directory_realitive_path_lookup(get_inode(ROOT_INUM), folders);
    slist_free(folders);
    return output;
}

//get all the files at the given path realitive to the given directory
dirent_t *directory_realitive_path_lookup(inode_t *di, slist_t *path)
{
    if (path->next == 0)
    { //if this was the desired folder
        return directory_lookup(di, path->data);
    }
    inode_t *next = get_inode(directory_lookup(di, path->data)->inum);
    return directory_realitive_path_lookup(next, path->next);
}

int directory_put(inode_t *di, const char *name, int inum, mode_t mode)
{
    printf("Putting %s assosiated with the number %d in the given directory\n", name, inum);
    int block = -1;
    header_t dir_header[1];
    for (int i = 0; i < di->size / BLOCK_SIZE; ++i)
    {
        assert(inode_read(di, dir_header, sizeof(header_t), i * BLOCK_SIZE) == sizeof(header_t));
        if (dir_header->free != 0)
        {
            block = i;
            break;
        }
    }

    header_t *header;
    if (block == -1)
    {
        dirent_t directory[DIR_PER_BLOCK];
        header = (header_t *)directory;
        header->free = DIR_PER_BLOCK - 1;
        bitmap_put(header->bm, 0, 1); //Position 0 is taken as the header
        for (int i = 1; i < DIR_PER_BLOCK; i++)
        {
            bitmap_put(header->bm, i, 0);
        }
        block = di->size / BLOCK_SIZE; //size is always divisible by BLOCK_SIZE
        assert(BLOCK_SIZE == inode_write(di, directory, BLOCK_SIZE, di->size));
    }
    dirent_t *directory = (dirent_t *)blocks_get_block(inode_get_bnum(di, block));
    header = (header_t *)directory;
    for (int i = 1; i < di->size / sizeof(dirent_t); ++i)
    {
        if (bitmap_get(header->bm, i % DIR_PER_BLOCK) == 0)
        {
            memcpy(directory[i].name, name, DIR_NAME_LENGTH);
            directory[i].name[DIR_NAME_LENGTH] = '\0'; //Caps the string to the array size
            directory[i].inum = inum;
            directory[i].mode = mode;
            bitmap_put(header->bm, i, 1);
            header->free -= 1;
            return 0;
        }
    }
    return 1;
}

int directory_delete(inode_t *di, const char *name)
{
    printf("Removing directory entry by the name of %s\n", name);
    dirent_t *directory = (dirent_t *)malloc(di->size);
    assert(di->size == inode_read(di, directory, di->size, 0)); //not modifiable
    for (int i = 1; i < di->size / sizeof(dirent_t); ++i)
    {
        header_t *header = (header_t *)(directory + DIR_PER_BLOCK * (i / DIR_PER_BLOCK)); //header of the portion
        if (0 == bitmap_get(header->bm, i % DIR_PER_BLOCK) || i % DIR_PER_BLOCK == 0)
        { //empty or full with the header
            continue;
        }

        if (strcmp(directory[i].name, name) == 0)
        {
            header_t *real = (header_t *)blocks_get_block(inode_get_bnum(di, i / DIR_PER_BLOCK)); //modifiable
            bitmap_put(real->bm, i % DIR_PER_BLOCK, 0);
            real->free += 1;
            free(directory);
            return 0;
        }
    }
    free(directory);
    return -1;
}

slist_t *directory_list(const char *path)
{
    printf("listing everything in %s\n", path);
    slist_t *folders = slist_explode(path[0] == '/' ? path + 1 : path, '/');
    slist_t *output = directory_realitive_list(get_inode(ROOT_INUM), folders);
    slist_free(folders);
    return output;
}

//get all the files at the given path realitive to the given directory
slist_t *directory_realitive_list(inode_t *di, slist_t *path)
{
    if (path == 0)
    { //if this was the desired folder
        printf("We've arrived\n");
        return directory_list_given(di);
    }
    printf("next looking for %s\n", path->data);
    inode_t *next = get_inode(directory_lookup(di, path->data)->inum);
    return directory_realitive_list(next, path->next);
}

slist_t *directory_list_given(inode_t *di)
{
    slist_t *output = NULL;
    dirent_t *directory = (dirent_t *)malloc(di->size);
    assert(di->size == inode_read(di, directory, di->size, 0));

    for (int i = 1; i < di->size / sizeof(dirent_t); ++i)
    {
        header_t *header = (header_t *)(directory + DIR_PER_BLOCK * (i / DIR_PER_BLOCK)); //header of the portion
        if (0 == bitmap_get(header->bm, i % DIR_PER_BLOCK) || i % DIR_PER_BLOCK == 0)
        { //empty or full with the header
            continue;
        }
        output = slist_cons(directory[i].name, output);
    }
    free(directory);
    return output;
}

void print_directory(inode_t *dd)
{
    dirent_t *directory = (dirent_t *)malloc(dd->size);
    assert(dd->size == inode_read(dd, directory, dd->size, 0));
    for (int i = 1; i < dd->size / sizeof(dirent_t); ++i)
    {
        void *pointer = (void *)(directory + DIR_PER_BLOCK * (i / DIR_PER_BLOCK)); //header of the portion
        header_t *header = (header_t *)pointer;
        if (0 == bitmap_get(header->bm, i % DIR_PER_BLOCK) || i % DIR_PER_BLOCK == 0)
        { //empty or full with the header
            continue;
        }
        printf("%s\n", directory[i].name);
    }
    free(directory);
}

void copy_folder(const char *path, char *buf, size_t size)
{
    int slash = 0;
    for (int i = 0; i < size; i++)
    {
        if (path[i] == '/')
        {
            slash = i; //the last backslash
        }
    }
    strncpy(buf, path, slash);
    buf[slash] = '\0';
}

void copy_file(const char *path, char *buf, size_t size)
{
    int slash = 0;
    for (int i = 0; i < size; i++)
    {
        if (path[i] == '/')
        {
            slash = i + 1; //the last backslash
        }
    }
    strncpy(buf, path + slash, size - slash);
    buf[size - slash] = '\0';
}