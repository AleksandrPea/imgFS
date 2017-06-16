#define HEADER_OFFSET 0

#include <stdlib.h>
#include <string.h>

#include "img-util.h"

static void initFAT(FSContext *context);
static BlockID allocateBlock(FSContext *context);
static BlockID addBlockFor(FileDescriptor *descr, FSContext *context);
static void removeBlocksFrom(FileDescriptor *descr, int blockN, FSContext *context);
static void releaseBlocksChain(BlockID startBlock, FSContext *context);
static int getBlocksChain(BlockID startBlock, BlockID *blockArr, FSContext *context);
static BlockID getBlockInChain(BlockID startBlock, int blockIndex, FSContext *context);

static int findLinkIn(FileDescriptor *dirDescr, char name[MAX_FNAME_LEN], long *deOffset, FSContext *context);

static void detachName(const char *path, char *dirPath, char *lastName);

static void fillHeaderIn(FSContext *context);
static void defineOffsets(FSContext *context);
static void grindFile(FILE *file, long size);


/** return created context*/
FSContext *createImgFile(char *imgPath, long devSize, int blockSize, int maxFileN) {
    FILE *imgFile = fopen(imgPath, "wb+");
    FSContext *context = malloc(sizeof(FSContext));
    context->imgFile = imgFile;
    context->devSize = devSize;
    context->blockSize = blockSize;
    context->maxFileN = maxFileN;
    defineOffsets(context);
    grindFile(imgFile, devSize);
    fillHeaderIn(context);
    initFAT(context);
    // making root dir descr
    FileDescriptor *root = malloc(sizeof(FileDescriptor));
    context->root = root;
    root->type = FT_DIRECTORY;
    root->size = 0;
    createDescriptor(root, context); 
    makeDefaultLinks(root, "/", context);
    return context;
}

void closeContext(FSContext *context) {
    fclose(context->imgFile);
    free(context->root);
    free(context);
}

FSContext *openContext(char* imgPath) {
    FSContext *context = malloc(sizeof(FSContext));
    FILE *imgFile = fopen(imgPath, "rb+");
    context->imgFile = imgFile;
    fseek(imgFile, HEADER_OFFSET, SEEK_SET);
    fread(&(context->devSize), sizeof(long),1, imgFile);
    fread(&(context->blockSize), sizeof(int),1, imgFile);
    fread(&(context->maxFileN), sizeof(int), 1, imgFile);
    defineOffsets(context);
    FileDescriptor *descr = malloc(sizeof(FileDescriptor));
    getDescriptor(descr, 0, context);
    context->root = descr;
    return context;
}

/**
 * Descr must have type, size filled.
 * return fdId of created descriptor.
 * returning -2 means, that number of descriptors have reached its maximum;
 *           -1 means, that there are no free blocks.
 */
int createDescriptor(FileDescriptor *descr, FSContext *context) {
    FILE *imgFile = context->imgFile;
    int maxFileN = context->maxFileN;
    int fdId = 0;
    bool found = false;
    FileDescriptor *readDescr = malloc(sizeof(FileDescriptor));
    fseek(imgFile, context->descriptorsOffset, SEEK_SET);
    while(fdId < maxFileN && !found) {
        fread(readDescr, sizeof(FileDescriptor), 1, imgFile);
        if (readDescr->type == FT_DELETED) {
            found = true;
        } else {
            fdId++;
        }
    }
    free(readDescr);
    if (fdId < maxFileN) {
        BlockID freeBlock = allocateBlock(context);
        if (freeBlock == -1) {
            fdId = -1;
        } else {
            descr->fdId = fdId;
            descr->nlink = 0;
            descr->firstBlock = freeBlock;
            descr->occupiedBlocks = 1;
            saveDescriptor(descr, context);
        }
    } else {
        fdId = -2;
    }
    return fdId;
}

void removeDescriptor(FileDescriptor *descr, FSContext *context) {
    if (descr->type == FT_DIRECTORY) {
        // deleting all entries
        DirEntry entry;
        int result = getEntryFrom(descr, &entry, context);
        while (result != -1) {
            entry.name[0] = -1;
            result = getEntryFrom(NULL, &entry, context);
        }
    }
    releaseBlocksChain(descr->firstBlock, context);
    descr->type = FT_DELETED;
    saveDescriptor(descr, context);
}

void saveDescriptor(FileDescriptor *descr, FSContext *context) {
    FILE *imgFile = context->imgFile;
    long offset = context->descriptorsOffset + descr->fdId*sizeof(FileDescriptor);
    fseek(imgFile, offset, SEEK_SET);
    fwrite(descr, sizeof(FileDescriptor), 1, imgFile);
}

void getDescriptor(FileDescriptor *descr, int fdId, FSContext *context) {
    FILE *imgFile = context->imgFile;
    long offset = context->descriptorsOffset + fdId*sizeof(FileDescriptor);
    fseek(imgFile, offset, SEEK_SET);
    fread(descr, sizeof(FileDescriptor), 1, imgFile);
}

/** 
 * descriptors points to memory with size = sizeof(FileDescriptor*)*maxFileN,
 * return: number of descriptors(not including deleted)
 */
int getAllDescriptors(FileDescriptor **descriptors, FSContext *context) {
    FILE *imgFile = context->imgFile;
    int maxFileN = context->maxFileN;
    int fdId = 0;
    int N = 0;
    fseek(imgFile, context->descriptorsOffset, SEEK_SET);
    while (fdId < maxFileN) {
        fread(descriptors[N], sizeof(FileDescriptor), 1, imgFile);
        if (descriptors[N]->type != FT_DELETED) {
            N++;
        }
        fdId++;
    }
    return N;
}

/** 
 * makes pFirstFree -> 2 -> 3 -> 4 etc.
 * Some of the first blocks will be occupied by header and others. 
 */
static void initFAT(FSContext *context) {
    FILE *imgFile = context->imgFile;
    int occupiedBlocks = context->dataOffset / context->blockSize +
                     (context->dataOffset % context->blockSize) ? 1 : 0;
    BlockID nextFree = occupiedBlocks;
    // 
    fseek(imgFile, context->fatOffset - sizeof(BlockID), SEEK_SET);
    fwrite(&nextFree, sizeof(BlockID), 1, imgFile);
    fseek(imgFile, context->fatOffset + nextFree*sizeof(BlockID), SEEK_SET);
    nextFree++;
    int blocksN = context->devSize / context->blockSize;
    for (;nextFree < blocksN; nextFree++) {
        fwrite(&nextFree, sizeof(BlockID), 1, imgFile);
    }
    nextFree = -1;
    fwrite(&nextFree, sizeof(BlockID), 1, imgFile);
}

/** 
 * return: id of allocated block. Changes FAT on the disk.
 *         -1 means, that there are no free blocks
 */
static BlockID allocateBlock(FSContext *context) {
    FILE *imgFile = context->imgFile;
    fseek(imgFile, context->fatOffset - sizeof(BlockID), SEEK_SET);
    BlockID freeBlock;
    fread(&freeBlock, sizeof(BlockID), 1, imgFile);
    if (freeBlock != -1) {
        // fetch freeBlock
        fseek(imgFile, context->fatOffset + freeBlock*sizeof(BlockID), SEEK_SET);
        BlockID nextBlock;
        fread(&nextBlock, sizeof(BlockID), 1, imgFile);
        fseek(imgFile, context->fatOffset + freeBlock*sizeof(BlockID), SEEK_SET);
        BlockID endCode = -1;
        fwrite(&endCode, sizeof(BlockID), 1, imgFile);
        fseek(imgFile, context->fatOffset - sizeof(BlockID), SEEK_SET);
        fwrite(&nextBlock, sizeof(BlockID), 1, imgFile);
    }
    return freeBlock;
}

/** 
 * Picks free block and grinds it(fills with zeroes).
 * descr must have right firstBlock field.
 * return: id of added block.
 *         -1 means, that there are no free blocks
 * Changes FAT on the disk.
 */
static BlockID addBlockFor(FileDescriptor *descr, FSContext *context) {
    FILE *imgFile = context->imgFile;
    BlockID freeBlock = allocateBlock(context);
    if (freeBlock != -1) {
        BlockID nextBlock = descr->firstBlock;
        BlockID currBlock;
        while (nextBlock != -1) {
            currBlock = nextBlock;
            fseek(imgFile, context->fatOffset + currBlock*sizeof(BlockID), SEEK_SET);
            fread(&nextBlock, sizeof(BlockID), 1, imgFile);
        }
        fseek(imgFile, context->fatOffset + currBlock*sizeof(BlockID), SEEK_SET);
        fwrite(&freeBlock, sizeof(BlockID), 1, imgFile);
        descr->occupiedBlocks++;
        saveDescriptor(descr, context);
        void *zeroes = malloc(context->blockSize);
        memset(zeroes, 0, context->blockSize);
        fseek(imgFile, context->dataOffset + freeBlock*context->blockSize, SEEK_SET);
        fwrite(zeroes, context->blockSize, 1, imgFile);
        free(zeroes);
    }
    return freeBlock;
}

/** 
 * Changes FAT on the disk. If blockN > descr->occupiedBlocks - removes all blocks.
 */
static void removeBlocksFrom(FileDescriptor *descr, int blockN, FSContext *context) {
    if (blockN > 0) {
        if (blockN >= descr->occupiedBlocks) {
            releaseBlocksChain(descr->firstBlock, context);
            descr->firstBlock = -1;
            descr->occupiedBlocks = 0;
        } else {
            FILE *imgFile = context->imgFile;
            int index = descr->occupiedBlocks - blockN - 1;
            BlockID block = getBlockInChain(descr->firstBlock, index, context);
            fseek(imgFile, context->fatOffset + block*sizeof(BlockID), SEEK_SET);
            BlockID chainToDel;
            fread(&chainToDel, sizeof(BlockID), 1, imgFile);
            releaseBlocksChain(chainToDel, context);
            fseek(imgFile, context->fatOffset + block*sizeof(BlockID), SEEK_SET);
            BlockID endcode = -1;
            fwrite(&endcode, sizeof(BlockID), 1, imgFile);
            descr->occupiedBlocks = descr->occupiedBlocks - blockN;
            saveDescriptor(descr, context);
        }
    }
} // not tested!!

/** 
 * freeBlocks points to size >= numberOfFreeBlocks*sizeof(BlockID)
 * return: number of free blocks. 
 * Doesn't modify FAT.
 */
int getFreeBlocks(BlockID *freeBlocks, FSContext *context) {
    FILE *imgFile = context->imgFile;
    fseek(imgFile, context->fatOffset - sizeof(BlockID), SEEK_SET);
    BlockID nextFree;
    fread(&nextFree, sizeof(BlockID), 1, imgFile);
    return getBlocksChain(nextFree, freeBlocks, context);
}

/** 
 * return: number of free blocks. 
 * Doesn't modify FAT.
 */
int numberOfFreeBlocks(FSContext *context) {
    FILE *imgFile = context->imgFile;
    fseek(imgFile, context->fatOffset - sizeof(BlockID), SEEK_SET);
    BlockID nextFree;
    fread(&nextFree, sizeof(BlockID), 1, imgFile);
    int number = 0;
    while (nextFree != -1) {
        number++;
        fseek(imgFile, context->fatOffset + nextFree*sizeof(BlockID), SEEK_SET);
        fread(&nextFree, sizeof(BlockID), 1, imgFile);
    }
    return number;
}

/**
 * Doesn't modify FAT.
 * arr points to size >= descr->occupiedBlocks*sizeof(BlockID)
 * return: number of occupiedBlocks(== descr->occupiedBlocks)
 */
int getBlocksOf(FileDescriptor *descr, BlockID *blockArr, FSContext *context) {
    return getBlocksChain(descr->firstBlock, blockArr, context);
}

static void releaseBlocksChain(BlockID startBlock, FSContext *context) {
    FILE *imgFile = context->imgFile;
    fseek(imgFile, context->fatOffset - sizeof(BlockID), SEEK_SET);
    BlockID pChain;
    fread(&pChain, sizeof(BlockID), 1, imgFile);
    // pFirstFree -> pChain
    fseek(imgFile, context->fatOffset - sizeof(BlockID), SEEK_SET);
    fwrite(&startBlock, sizeof(BlockID), 1, imgFile);
    // now pFirstFree -> startBlock
    BlockID currBlock;
    BlockID nextBlock = startBlock;
    while (nextBlock != -1) {
        currBlock = nextBlock;
        fseek(imgFile, context->fatOffset + currBlock*sizeof(BlockID), SEEK_SET);
        fread(&nextBlock, sizeof(BlockID), 1, imgFile);
    }
    fseek(imgFile, context->fatOffset + currBlock*sizeof(BlockID), SEEK_SET);
    fwrite(&pChain, sizeof(BlockID), 1, imgFile);
    // now pFirstFree -> startBlock ..... -> pChain
}

/** return: size of chain(N of blocks) */
static int getBlocksChain(BlockID startBlock, BlockID *blockArr, FSContext *context) {
    FILE *imgFile = context->imgFile;
    BlockID *arr = blockArr;
    BlockID nextFree = startBlock;
    int blocksN = 0;
    while (nextFree != -1) {
        arr[blocksN] = nextFree;
        blocksN++;
        fseek(imgFile, context->fatOffset + nextFree*sizeof(BlockID), SEEK_SET);
        fread(&nextFree, sizeof(BlockID), 1, imgFile);
    }
    return blocksN;
}

static BlockID getBlockInChain(BlockID startBlock, int blockIndex, FSContext *context) {
    FILE *imgFile = context->imgFile;
    BlockID block = startBlock;
    for (int i = 0; i < blockIndex; i++) {
            fseek(imgFile, context->fatOffset + block*sizeof(BlockID), SEEK_SET);
            fread(&block, sizeof(BlockID), 1, imgFile);
    }
    return block;
}

/** 
 * Adds memory as more as it is possible up to newSize.
 * return: delta of new and old sizes. 
 * Modifies FAT
 */
int changeSize(FileDescriptor *descr, int newSize, FSContext *context) {
    int delta;
    int newBlocksN = newSize / context->blockSize + (newSize % context->blockSize > 0 ? 1 : 0);
    if (descr->size > newSize) {
        int deltaBlocks = descr->occupiedBlocks - newBlocksN;
        removeBlocksFrom(descr, deltaBlocks, context);
        delta = descr->size-newSize;
        descr->size = newSize;
    } else if (descr->size < newSize) {
        int deltaBlocks = newBlocksN - descr->occupiedBlocks;
        BlockID block = 0;
        int i = 0;
        while (i < deltaBlocks && block != -1) {
            block = addBlockFor(descr, context);
            i++;
        }
        delta = newSize - descr->size;
        int oldSize = descr->size;
        descr->size = newSize;
        if (block == -1) {
            // нужно проверить это:
            descr->size -= (deltaBlocks - i)*context->blockSize;
            delta = oldSize - descr->size; 
        }
   
    }
    saveDescriptor(descr, context);
    return delta;
}

/** return: written size(in bytes). 0 means, that there is not enough space*/
size_t writeTo(FileDescriptor *descr, const void *buf, size_t size, int offsetInFile, FSContext *context) {
    FILE *imgFile = context->imgFile;
    int blockIndex = offsetInFile / context->blockSize;
    int offsetInBlock = offsetInFile % context->blockSize;

    size_t portion;
    if (offsetInBlock + size > context->blockSize) {
        // manage writing with size > blockSize
        portion = context->blockSize - offsetInBlock;
    } else {
        portion = size;
    }
    int blocksToAdd = blockIndex + (size - portion)/context->blockSize - descr->occupiedBlocks + 1;
    size_t writtenSize;
    if (blocksToAdd < numberOfFreeBlocks(context)) {
        for (int i = 0; i < blocksToAdd; i++) {
            addBlockFor(descr, context);
        }
        void *buffer = buf;
        BlockID block = getBlockInChain(descr->firstBlock, blockIndex, context);
        fseek(imgFile, context->dataOffset + block*context->blockSize + offsetInBlock, SEEK_SET);
        writtenSize = fwrite(buffer, portion, 1, imgFile)*portion;
        buffer = (void*)((char*) buffer + writtenSize);
        size -= portion;
        if (size > 0) {
            // writing full block-sized info
            while (size > context->blockSize) {
               // picking next block
               block = getBlockInChain(block, 1, context);
               fseek(imgFile, context->dataOffset + block*context->blockSize, SEEK_SET);
               size_t part = fwrite(buffer, context->blockSize, 1, imgFile)*context->blockSize;
               writtenSize += part;
               buffer = (void*)((char*) buffer + part);
               size -= context->blockSize;
            }
            block = getBlockInChain(block, 1, context);
            fseek(imgFile, context->dataOffset + block*context->blockSize, SEEK_SET);
            writtenSize += fwrite(buffer, size, 1, imgFile)*size;
        }
    } else {
        writtenSize = 0;
    }
    return writtenSize;
}

/** return: read size(in bytes). 0 means, that (offsetInFile+size) is beyond size of the file */
size_t readFrom(FileDescriptor *descr, void *buf, size_t size, int offsetInFile, FSContext *context) {
    FILE *imgFile = context->imgFile;
    int blockIndex = offsetInFile / context->blockSize;
    int offsetInBlock = offsetInFile % context->blockSize;
    
    // manage reading with size > blockSize
    size_t portion;
    if (offsetInBlock + size > context->blockSize) {
        portion = context->blockSize - offsetInBlock;
    } else {
        portion = size;
    }
    int lastReadBlockIndex  = blockIndex + (size - portion)/context->blockSize;
    size_t readSize;
    if (lastReadBlockIndex < descr->occupiedBlocks) {
        // reading left tail
        void *buffer = buf;
        BlockID block = getBlockInChain(descr->firstBlock, blockIndex, context);
        fseek(imgFile, context->dataOffset + block*context->blockSize + offsetInBlock, SEEK_SET);
        readSize = fread(buffer, portion, 1, imgFile)*portion;
        buffer = (void*)((char*) buffer + readSize);
        size -= portion;
        if (size > 0) {
            // reading full block-sized info
            while (size > context->blockSize) {
               // picking next block
               block = getBlockInChain(block, 1, context);
               fseek(imgFile, context->dataOffset + block*context->blockSize, SEEK_SET);
               size_t part = fread(buffer, context->blockSize, 1, imgFile)*context->blockSize;
               readSize += part;
               buffer = (void*)((char*) buffer + part);
               size -= context->blockSize;
            }
            // reading right tail
            block = getBlockInChain(block, 1, context);
            fseek(imgFile, context->dataOffset + block*context->blockSize, SEEK_SET);
            readSize += fread(buffer, size, 1, imgFile)*size;
        }
    } else {
        readSize = 0;
    }
    return readSize;
}

/** increments nlink */
void writeDirEntryTo(FileDescriptor *dirDescr, DirEntry *record, FSContext *context) {
    DirEntry readRecord;
    int offset = 0;
    readFrom(dirDescr, &readRecord, sizeof(DirEntry), offset, context);
    // first char = FFFF means that record is deleted; 0000 means EOF
    while (readRecord.name[0] != -1 && readRecord.name[0] != 0) {
        offset += sizeof(DirEntry);
        readFrom(dirDescr, &readRecord, sizeof(DirEntry), offset, context);
    }
    writeTo(dirDescr, record, sizeof(DirEntry), offset, context);
    FileDescriptor descr;
    getDescriptor(&descr, record->fdId, context);
    descr.nlink++;
    saveDescriptor(&descr, context);
}

/** 
 * removes dir enrty by specified name in specified directory
 * return: 0 if succes, else -1.
 * decrements nlink and removes associated descriptor if nlink reaches 0.
 */
int deleteDirEntryIn(FileDescriptor *dirDescr, char name[MAX_FNAME_LEN], FSContext *context) {
    long offset;
    int fdId = findLinkIn(dirDescr, name, &offset, context);
    int rcode;
    if (fdId != -1) {
        DirEntry record;
        record.name[0] = -1;
        writeTo(dirDescr, &record, sizeof(DirEntry), offset, context);
        FileDescriptor descr;
        getDescriptor(&descr, fdId, context);
        descr.nlink--;
        if (descr.nlink == 0) {
            removeDescriptor(&descr, context);
        }
        rcode = 0;
    } else {
        rcode = -1;
    }
    return rcode;
}

void makeLink(FileDescriptor *descr, const char *path, FSContext *context) {
    char *dirPath = malloc((strlen(path)+1)*sizeof(char));
    DirEntry record;
    record.fdId = descr->fdId;
    detachName(path, dirPath, record.name);
    FileDescriptor dirDescr;
    getDescriptorByPath(&dirDescr, dirPath, context);
    writeDirEntryTo(&dirDescr, &record, context);
    free(dirPath);
}

/**
 * makes "." and ".." links. Plus makes link to path.
 * return 0 if success, else -1 - dirDescr type is not FT_DIRECTORY
 */
int makeDefaultLinks(FileDescriptor *dirDescr, const char *path, FSContext *context) {
    int rcode;
    if (dirDescr->type == FT_DIRECTORY) {
        DirEntry record;
        record.fdId = dirDescr->fdId;
        strcpy(record.name, ".");
        writeDirEntryTo(dirDescr, &record, context);
        strcpy(record.name, "..");
        if (strcmp(path, "/") == 0) {
            writeDirEntryTo(dirDescr, &record, context);
        } else {
            FileDescriptor parentDir;
            char *parentPath = malloc((strlen(path)+1)*sizeof(char));
            char name[MAX_FNAME_LEN];
            detachName(path, parentPath, name);
            getDescriptorByPath(&parentDir, parentPath, context);
            free(parentPath);
            record.fdId = parentDir.fdId;
            writeDirEntryTo(dirDescr, &record, context);
            record.fdId = dirDescr->fdId;
            strcpy(record.name, name);
            writeDirEntryTo(&parentDir, &record, context);
        }
        rcode = 0;
    } else {
        rcode = -1;
    }
    return rcode;
}

void removeLink(const char *path, FSContext *context) {
    char *dirPath = malloc((strlen(path)+1)*sizeof(char));
    char name[MAX_FNAME_LEN];
    detachName(path, dirPath, name);
    FileDescriptor dirDescr;
    getDescriptorByPath(&dirDescr, dirPath, context);
    deleteDirEntryIn(&dirDescr, name, context);
    free(dirPath);
}

/** ex: path = /dir/file => dirPath = /dir, lastName = file. */
static void detachName(const char *path, char *dirPath, char *lastName) {
    char *fileName;
    strcpy(dirPath, path);
    fileName = strrchr(dirPath, '/');
    fileName[0] = 0;
    fileName++;
    strcpy(lastName, fileName);
    if (dirPath[0] == 0) {
        dirPath[0] = '/'; dirPath[1] = 0;
    }
}

/** 
 * searches dir entry by specified name in specified directory
 * return: id of linked descriptor, or -1 if not found.
 *          and offset of DirEnry in deOffset param
 */
static int findLinkIn(FileDescriptor *dirDescr, char name[MAX_FNAME_LEN], long *deOffset, FSContext *context) {
    long offset = 0;
    DirEntry readRecord;
    readFrom(dirDescr, &readRecord, sizeof(DirEntry), offset, context);
    // first char = 0000 means EOF
    while (strcmp(readRecord.name, name) != 0 && readRecord.name[0] != 0) {
        offset += sizeof(DirEntry);
        readFrom(dirDescr, &readRecord, sizeof(DirEntry), offset, context);
    }
    int fdId;
    if (readRecord.name[0] != 0) {
        fdId = readRecord.fdId;
        if (deOffset != NULL) {
            *deOffset = offset;
        }
    } else {
        fdId = -1;
    }
    return fdId;
}

/** 
 * searches hard link by specified ABSOLUTE path and
 * writes found descriptor in descr struct
 * return: id of linked descriptor, or -1 if not found.
 */
int getDescriptorByPath(FileDescriptor *descr, const char *path, FSContext *context) {
    int fdId;
    if (strcmp(path, "/") == 0) {
        memcpy(descr, context->root, sizeof(FileDescriptor));
        fdId = descr->fdId;
    } else {
        char delim[2] = "/";
        char *name;
        char *pathCopy = malloc((strlen(path)+1)*sizeof(char));
        strcpy(pathCopy, path);
        FileDescriptor currentDir;
        memcpy(&currentDir, context->root, sizeof(FileDescriptor));
        name = strtok(pathCopy, delim);
        bool rightPath = true;
        while( name != NULL  && rightPath) {
            fdId = findLinkIn(&currentDir, name, NULL, context);
            if (fdId != -1) {
                getDescriptor(descr, fdId, context);
                if (descr->type == FT_DIRECTORY) {
                    memcpy(&currentDir, descr, sizeof(FileDescriptor));
                }
                name = strtok(NULL, delim);
            } else {
                rightPath = false;
            }
        }
        free(pathCopy);
    }
    return fdId;
}

/** 
 * This method helps to iterate over entries in specified directory.
 * If nullPointer is passed in dirDescr than the previous passed descriptor is used.
 * return: 0, or -1 if there are no entries
 */
int getEntryFrom(FileDescriptor *dirDescr, DirEntry *entry, FSContext *context) {
    static FileDescriptor *descr;
    static int offset;
    if (dirDescr != NULL) {
        descr = dirDescr;
        offset = 0;
    }
    readFrom(descr, entry, sizeof(DirEntry), offset, context);
    while (entry->name[0] == -1 && entry->name[0] != 0) {
        offset += sizeof(DirEntry);
        readFrom(descr, entry, sizeof(DirEntry), offset, context);
    }
    int returnCode;
    if (entry->name[0] != 0) {
        offset += sizeof(DirEntry);
        returnCode = 0;
    } else {
        returnCode = -1;
    }
    return returnCode;
}

static void fillHeaderIn(FSContext *context) {
    FILE *imgFile = context->imgFile;
    fseek(imgFile, HEADER_OFFSET, SEEK_SET);
    fwrite(&(context->devSize), sizeof(long),1, imgFile);
    fwrite(&(context->blockSize), sizeof(int),1, imgFile);
    fwrite(&(context->maxFileN), sizeof(int),1, imgFile);
}

static void defineOffsets(FSContext *context) {
    context->descriptorsOffset = HEADER_OFFSET + 2*sizeof(int) + sizeof(long);
    context->fatOffset = context->descriptorsOffset + context->maxFileN*sizeof(FileDescriptor)
                        + sizeof(BlockID); // therefore, [fatOffset - sizeof(BlockID)] points to pointer to 1st free block
    int fatSize = (context->devSize / context->blockSize)*sizeof(BlockID);
    context->dataOffset = context->fatOffset + fatSize;
}

/** size in bytes */
static void grindFile(FILE *file, long size) {
    long kbs = size/1024;
    int intsInKb = 1024/sizeof(int);
    int *zeroes = malloc(intsInKb*(sizeof(int)));
    for (int i = 1; i <= kbs; i++) {
        fwrite(zeroes, sizeof(int), intsInKb, file);
    }
    free(zeroes);
}