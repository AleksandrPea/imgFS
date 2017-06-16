#ifndef _LOG_H_
#define _LOG_H_

#include "img-util.h"

void dumpFS(FSContext *context);
void printDescriptor(FileDescriptor *descr);
char *fileTypeToStr(FileType ft);

#endif