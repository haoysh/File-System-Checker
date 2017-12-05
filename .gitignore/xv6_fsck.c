#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <assert.h>
#include <dirent.h>
#include <stdbool.h>
#include <sys/mman.h>


#define NDIRECT 12
#define BSIZE 512
// File system super block
struct superblock {
  uint size;         // Size of file system image (blocks)
  uint nblocks;      // Number of data blocks
  uint ninodes;      // Number of inodes.
};

// On-disk inode structure
struct dinode {
  short type;           // File type
  short major;          // Major device number (T_DEV only)
  short minor;          // Minor device number (T_DEV only)
  short nlink;          // Number of links to inode in file system
  uint size;            // Size of file (bytes)
  uint addrs[NDIRECT+1];   // Data block addresses
};

int main (int argc, char *argv[]){
  if(argc != 2){
    exit(1);
  }

  int fd = open(argv[1], O_RDONLY);
  if(fd < 0){
    fprintf(stderr,"image not found.\n");
    exit(1);
  }

  int rc;
  struct stat sbuf;
  rc = fstat(fd, &sbuf);
  assert(rc == 0);
 
  void *img_ptr = mmap(NULL, sbuf.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
  //assert

  struct superblock *sb;
  sb = (struct superblock *) (img_ptr + BSIZE);
  
  struct dinode *dip = (struct dinode *) (img_ptr + 2*BSIZE);
  for(int i = 0; i < sb->ninodes; i++){
    if(dip->type > 3){
      fprintf(stderr, "ERROR: bad inode.\n");
      exit(1);
    }
    dip++;
  }

  return 0;
}
