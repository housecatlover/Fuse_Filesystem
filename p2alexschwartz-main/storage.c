#include "directory.h"
#include "storage.h"
#include "bitmap.h"

void storage_init(const char *path)
{
    printf("init blocks\n");
    blocks_init(path);
    printf("init root directory\n");
    directory_init();

    printf("done initilizing\n");
}