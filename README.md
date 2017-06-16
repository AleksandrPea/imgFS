# imgFS
Custom filesystem based on FUSE

## FS description
This is example of making FUSE based FS that is called imgFS. Idea of block storage device is used - each filesystem is saved into file(image). Files in this FS is preserved internally like in FAT.
Image is divided into header, descriptors section, FAT and data.
### Implemented features
- *create/rename/delete* files
- *open/read/write* files
- *create/rename/delete* directories
- *open/read* directories
- *hard links*
- *soft links*

## Required dependencies
- GCC or Clang
- CMake >= 3
- make
- FUSE 2.6 or later
- FUSE development files
If you have Linux you may run next command:
  Fedora/CentOS
``` yum install gcc fuse fuse-devel make cmake ```
  Debian/Ubuntu
``` apt-get install gcc fuse libfuse-dev make cmake ```

## Building
Setting environment and generating make-files:
``` cmake -DCMAKE_BUILD_TYPE=Debug . ```
Building project:
``` make -j ```

## Running FS
Creating image:
``` ./bin/imgFS crImg <path to image> <image size in MB> <block size in KB> <max number of files> ```
Mounting FS to some folder:
``` ./bin/imgFS -d -s -f <path to image> <folder to mount> ```
