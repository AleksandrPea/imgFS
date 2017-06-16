#ifndef _IMG_UTIL_H_
#define _IMG_UTIL_H_

#define bool char
#define true 1
#define false 0
     
#define MAX_FNAME_LEN 128

#include <stdio.h>

typedef int BlockID;

typedef enum { FT_DELETED=0, FT_REGULAR, FT_DIRECTORY, FT_SYMLINK} FileType;

typedef struct {
    int fdId;
    FileType type;
    int size;
    int nlink;
    BlockID firstBlock;
    int occupiedBlocks;
} FileDescriptor;

typedef struct {
    char name[MAX_FNAME_LEN];
    int fdId;
} DirEntry;

typedef struct {
    FILE *imgFile;
    long devSize;
    int blockSize;
    int maxFileN;
    long descriptorsOffset;
    long fatOffset;
    long dataOffset;
    FileDescriptor *root;
} FSContext;

FSContext *createImgFile(char *imgPath, long devSize, int blockSize, int maxFileN);
void closeContext(FSContext *context);
FSContext *openContext(char* imgPath);

int createDescriptor(FileDescriptor *descr, FSContext *context);
void removeDescriptor(FileDescriptor *descr, FSContext *context);
void saveDescriptor(FileDescriptor *descr, FSContext *context);
void getDescriptor(FileDescriptor *descr, int fdId, FSContext *context);
int getAllDescriptors(FileDescriptor **descriptors, FSContext *context);

int numberOfFreeBlocks(FSContext *context);
int getFreeBlocks(BlockID *freeBlocks, FSContext *context);
int getBlocksOf(FileDescriptor *descr, BlockID *blockArr, FSContext *context);

size_t writeTo(FileDescriptor *descr, const void *buf, size_t size, int offsetInFile, FSContext *context);
size_t readFrom(FileDescriptor *descr, void *buf, size_t size, int offsetInFile, FSContext *context);
void writeDirEntryTo(FileDescriptor *dirDescr, DirEntry *record, FSContext *context);
int getEntryFrom(FileDescriptor *dirDescr, DirEntry *entry, FSContext *context);

int getDescriptorByPath(FileDescriptor *descr, const char *path, FSContext *context);
void makeLink(FileDescriptor *from, const char *to, FSContext *context);
int makeDefaultLinks(FileDescriptor *dirDescr, const char *path, FSContext *context);
void removeLink(const char *path, FSContext *context);

int changeSize(FileDescriptor *descr, int newSize, FSContext *context);

#endif