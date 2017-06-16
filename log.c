#include <stdlib.h>

#include "log.h"

static void printBlockIDArray(BlockID *blockArr, int blockN);

void dumpFS(FSContext *context) {
    printf("<<<<<<<<<<<Img_FS>>>>>>>>>>>\n");
    printf("Dev size = %d Mbs\n", context->devSize/(1024*1024));
    printf("Block size = %d Kbs\n", context->blockSize/1024);
    printf("Maximum file number = %d\n", context->maxFileN);
    printf("--------------------------\n");
    int maxFileN = context->maxFileN;
    FileDescriptor **descriptors = malloc(maxFileN*sizeof(FileDescriptor*));
    for (int i = 0; i < maxFileN; i++) {
        descriptors[i] = malloc(context->maxFileN*sizeof(FileDescriptor));
    }
    int descriptorsN = getAllDescriptors(descriptors, context);
    BlockID *blocksArr = malloc((context->devSize/context->blockSize)*sizeof(BlockID));
    int blocksN;
    for (int i = 0; i < descriptorsN; i++) {
        printDescriptor(descriptors[i]);
        blocksN = getBlocksOf(descriptors[i], blocksArr, context);
        printf("Blocks: ");
        printBlockIDArray(blocksArr, blocksN);
    }
    blocksN = getFreeBlocks(blocksArr, context);
    printf("--------------------------\n");
    printf("Free blocks(%d): ", blocksN);
    printBlockIDArray(blocksArr, 20);
    for (int i = 0; i < maxFileN; i++) {
        free(descriptors[i]);
    }
    free(descriptors);
    free(blocksArr); 
}

static void printBlockIDArray(BlockID *blockArr, int blockN) {
    printf("%d", blockArr[0]);
    for (int i = 1; i < blockN; i++) {
        printf(", %d", blockArr[i]);
    }
    printf("\n");
}

void printDescriptor(FileDescriptor *descr) {
    printf("FD #%d of type %s\n", descr->fdId, fileTypeToStr(descr->type));
    printf("size = %d, nlink = %d\n", descr->size, descr->nlink);
}

char *fileTypeToStr(FileType ft) {
    char* result;
    switch (ft) {
      case FT_DELETED: result = "Deleted"; break;
      case FT_DIRECTORY: result = "Directory"; break;
      case FT_REGULAR: result = "Regular"; break;
      case FT_SYMLINK: result = "Symlink"; break;
    }
    return result;
}