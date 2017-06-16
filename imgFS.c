#define FUSE_USE_VERSION 26
    
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <fuse.h>
#include <libgen.h>
#include <limits.h>
#include <unistd.h>
#include <sys/types.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>

#include "img-util.h"
#include "log.h"

FSContext *context;

static int getattr_callback(const char *path, struct stat *stbuf) {
//  stbuf->st_uid = getuid();
//	stbuf->st_gid = getgid();
//	stbuf->st_atime = time( NULL );
//	stbuf->st_mtime = time( NULL );

    FileDescriptor descr;
    int fdId = getDescriptorByPath(&descr, path, context);
    if (fdId != -1) {
        if (descr.type == FT_DIRECTORY) {
            stbuf->st_mode = S_IFDIR | 0777;
        } else if (descr.type == FT_SYMLINK) {
            stbuf->st_mode = S_IFLNK | 0777; 
        } else {
            stbuf->st_mode = S_IFREG | 0777;
            stbuf->st_size = descr.size;
            stbuf->st_nlink = descr.nlink;
        }
        return 0;
    } else {
        return -ENOENT;
    }
}

static int open_callback(const char *path, struct fuse_file_info *fi) {
    FileDescriptor descr;
    int fdId = getDescriptorByPath(&descr, path, context);
    if (fdId != -1) {
       fi->fh = fdId;
       return 0;
    } else {
        return -ENOENT;
    }
}

static int release_callback(const char* path, struct fuse_file_info *fi) {
    return 0;
}

int opendir_callback(const char* path, struct fuse_file_info* fi) {
    return open_callback(path, fi);
}

static int releasedir_callback(const char* path, struct fuse_file_info *fi) {
    return release_callback(path, fi);
}


static int readdir_callback(const char *path, void *buf, fuse_fill_dir_t filler,
        off_t offset, struct fuse_file_info *fi) {
    FileDescriptor dirDescr;
    getDescriptor(&dirDescr, fi->fh, context);
    DirEntry entry;
    int result = getEntryFrom(&dirDescr, &entry, context);
    while (result != -1) {
        filler(buf, entry.name, NULL, 0);
        result = getEntryFrom(NULL, &entry, context);
    }
    return 0;
}

static int write_callback(const char *path, const char *buf, size_t size, off_t offset,
        struct fuse_file_info *fi) {
            
    if (fi->fh != 0) {
        FileDescriptor descr;
        getDescriptor(&descr, fi->fh, context);
        int result = writeTo(&descr, buf, size, offset, context);
        descr.size += result;            // костыль
        saveDescriptor(&descr, context);
        return result;
    } else {
        return -ENOENT;
    }
}

static int truncate_callback (const char *path, off_t size) {
    FileDescriptor descr;
    int fdId = getDescriptorByPath(&descr, path, context);
    if (fdId != -1) {
        if (size != 0) {  // костыль
            changeSize(&descr, size, context);
        }
        return 0;
    } else {
        return -ENOENT;
    }
}

static int read_callback(const char *path, char *buf, size_t size, off_t offset,
        struct fuse_file_info *fi) {
    
    if (fi->fh != 0) {
        FileDescriptor descr;
        getDescriptor(&descr, fi->fh, context);
        int result = readFrom(&descr, buf, size, offset, context);
        return result;
    } else {
        return -ENOENT;
    }
}

static int symlink_callback(const char* to, const char* from) {
    FileDescriptor descr;
    descr.type = FT_SYMLINK;
    descr.size = strlen(to) + 1;
    int fdId = createDescriptor(&descr, context);
    if (fdId == -2) {
        return -ENFILE;
    } else if (fdId == -1) {
        return -EOVERFLOW;
    } else {
        makeLink(&descr, from, context);
        writeTo(&descr, to, strlen(to) + 1, 0, context);
        return 0;
    }
    
}

static int readlink_callback(const char* path, char* buf, size_t size) {
    FileDescriptor descr;
    int fdId = getDescriptorByPath(&descr, path, context);
    if (fdId != -1) {
        if (descr.type != FT_SYMLINK) {
            return -EINVAL;
        } else {
            readFrom(&descr, buf, size, 0, context);
            return 0;
        }
    } else {
        return -ENOENT;
    }
}

static int link_callback(const char* from, const char* to) {
    FileDescriptor descr;
    int fdId = getDescriptorByPath(&descr, from, context);
    if (fdId != -1) {
        if (descr.type == FT_DIRECTORY) {
            return -EPERM;
        } else {
            makeLink(&descr, to ,context);
            return 0;
        }
    } else {
        return -ENOENT;
    }
}

static int unlink_callback(const char* path) {
    FileDescriptor descr;
    int fdId = getDescriptorByPath(&descr, path, context);
    if (fdId != -1) {
        if (descr.type == FT_DIRECTORY) {
            return -EISDIR;
        } else {
            removeLink(path, context);
            return 0;
        }
    } else {
        return -ENOENT;
    }
}

static int rmdir_callback(const char* path) {
    FileDescriptor descr;
    int fdId = getDescriptorByPath(&descr, path, context);
    if (fdId != -1) {
        DirEntry record;
        int result = getEntryFrom(&descr, &record, context);
        bool isEmpty = true;
        while (result != -1 && isEmpty) {
            if (strcmp(record.name, ".") != 0 && strcmp(record.name, "..") != 0) {
                isEmpty = false;
            }
            result = getEntryFrom(NULL, &record, context);
        }
        if (isEmpty) {
            removeLink(path, context);
            removeDescriptor(&descr, context);
            return 0;
        } else {
            return -ENOTEMPTY;
        }
    } else {
        return -ENOENT;
    }
    
}

static int mkdir_callback(const char* path, mode_t mode) {
    FileDescriptor descr;
    descr.type = FT_DIRECTORY;
    descr.size = 0;
    int fdId = createDescriptor(&descr, context);
    if (fdId == -2) {
        return -ENFILE;
    } else if (fdId == -1) {
        return -EOVERFLOW;
    } else {
        makeDefaultLinks(&descr, path, context);
        return 0;
    }
}

static int create_callback(const char* path, mode_t mode, struct fuse_file_info *fi) {
    if (strstr(path, "/.") == NULL) {
        int result = open_callback(path, fi);
        if (result != 0) {
            FileDescriptor descr;
            descr.type = FT_REGULAR;
            descr.size = 0;
            int fdId = createDescriptor(&descr, context);
            if (fdId == -2) {
                return -ENFILE;
            } else if (fdId == -1) {
                return -EOVERFLOW;
            } else {
                makeLink(&descr, path, context);
                fi->fh = fdId;
            }
        }
        return 0;
    } else {
        return -ENOENT;
    }
}

static int rename_callback(const char* from, const char* to) {
    FileDescriptor descr;
    int fdId = getDescriptorByPath(&descr, from, context);
    if (fdId != -1) {
        makeLink(&descr, to ,context);
        removeLink(from, context);
        return 0;
    } else {
        return -ENOENT;
    }
}

static void destroy_callback(void* private_data) {
    closeContext(context);
}

static struct fuse_operations fuse_example_operations = {
  .getattr = getattr_callback,
  .open = open_callback,
  .release = release_callback,
  .opendir = opendir_callback,
  .releasedir = releasedir_callback,
  .readdir = readdir_callback,
  .read = read_callback,
  .write = write_callback,
  .truncate = truncate_callback,
  .symlink = symlink_callback,
  .readlink = readlink_callback,
  .link = link_callback,
  .unlink = unlink_callback,
  .rmdir = rmdir_callback,
  .mkdir = mkdir_callback,
  .create = create_callback,
  .rename = rename_callback,
  .destroy = destroy_callback
};

void someTst(FSContext *context) {
    FileDescriptor descr;
    descr.type = FT_REGULAR;
    descr.size = 12;
    createDescriptor(&descr, context);
    FileDescriptor descr2;
    descr2.type = FT_REGULAR;
    descr2.size = 256;
    createDescriptor(&descr2, context);
    char text[13];
    strcpy(text, "Hello world!");
    writeTo(&descr, text, 13, 0, context);
    char text2[256];
    memset(text2, 0, sizeof(char)*256);
    strcpy(text2, "THIS is a txt file!");
    writeTo(&descr2, text2, 256, 0, context);
    DirEntry record;
    record.fdId = descr.fdId;
    strcpy(record.name, "file");
    writeDirEntryTo(context->root, &record, context);
    record.fdId = descr2.fdId;
    strcpy(record.name, "my.txt");
    writeDirEntryTo(context->root, &record, context);
}


int main(int argc, char *argv[]) {
    if (strcmp(argv[1],"crImg") == 0) {
        context = createImgFile(argv[2],atoi(argv[3])*1024*1024,atoi(argv[4])*1024,atoi(argv[5]));
        someTst(context);
        dumpFS(context);
        closeContext(context);
        return 0;
    } else {
        context = openContext(argv[argc-2]);
        dumpFS(context);
        //closeContext(context);
        argv[argc-2] = argv[argc-1];
        argc--;
        return fuse_main(argc, argv, &fuse_example_operations, NULL);
    }
}