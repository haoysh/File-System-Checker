#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <assert.h>
#include <stdbool.h>
#include <sys/mman.h>
#include "fs.h"
#include "types.h"

#define min(a, b) ((a) < (b) ? (a) : (b))
int fd;
void* img_ptr;

uint
xint(uint x)
{
  uint y;
  uchar *a = (uchar*)&y;
  a[0] = x;
  a[1] = x >> 8;
  a[2] = x >> 16;
  a[3] = x >> 24;
  return y;
}

ushort
xshort(ushort x)
{
  ushort y;
  uchar *a = (uchar*)&y;
  a[0] = x;
  a[1] = x >> 8;
  return y;
}
void
wsect(int sec, void *buf)
{
  lseek(fd, sec * 512L, SEEK_SET);
  write(fd, buf, 512);
}
uint
i2b(uint inum)
{
  return (inum / IPB) + 2;
}
void
rsect(int sec, void *buf)
{
  lseek(fd, sec * 512L, SEEK_SET);
  read(fd, buf, 512);
}
void
winode(uint inum, struct dinode *ip)
{
  char buf[512];
  uint bn;
  struct dinode *dip;

  bn = i2b(inum);
  rsect(bn, buf);
  dip = ((struct dinode*)buf) + (inum % IPB);
  *dip = *ip; //ip is modified i pointer
  wsect(bn, buf);
}
void
rinode(uint inum, struct dinode *ip)
{
  char buf[512];
  uint bn;
  struct dinode *dip;

  bn = i2b(inum);
  rsect(bn, buf);
  dip = ((struct dinode*)buf) + (inum % IPB);
  *ip = *dip;
}

void
iappend(uint inum, void *xp, int n)
{
  char *p = (char*)xp;
  uint fbn, off, n1;
  struct dinode din;
  char buf[512];
  uint x;

  rinode(inum, &din);

  off = xint(din.size);
  while(n > 0){
    
    fbn = off / 512;
    assert(fbn < MAXFILE);
    x = xint(din.addrs[fbn]);
    n1 = min(n, (fbn + 1) * 512 - off);
    rsect(x, buf);
    bcopy(p, buf + off - (fbn * 512), n1);
    wsect(x, buf);
    n -= n1;
    off += n1;
    p += n1;
  }
  din.size = xint(off);
  winode(inum, &din);
}

void repairx(int fd, int inode, struct dinode* lf, struct dinode* ip, int lostcount){
  
  char buf[BSIZE];
  struct dirent *entry = (struct dirent*)(lf->addrs[0]*BSIZE + img_ptr);
  char lost[DIRSIZ]; //Name making
  sprintf(lost, "lost_%d", lostcount);
  if(ip->type == 1){
    lseek(fd, ip->addrs[0] * BSIZE, SEEK_SET);
    read(fd, buf, BSIZE);
    struct dirent *entry2 = (struct dirent*) buf;
    (entry2+1)->inum = entry->inum;
    lseek(fd, ip->addrs[0]* BSIZE, SEEK_SET);
    write(fd, entry2, BSIZE); 
  }
  struct dirent de;
  bzero(&de, sizeof(de));
  de.inum = xshort(inode);
  strcpy(de.name, lost);
  iappend(entry->inum, &de, sizeof(de));
}

void printUsage(){
  fprintf(stderr,"Usage1: ./xv6_fsck \"-r\" [img file to repair]\n");
  fprintf(stderr,"Usage2: ./xv6_fsck [img file to check]\n");
  exit(1);
}

int main (int argc, char *argv[]){
  int repair = 0;
  if(argc == 3){
    if(strcmp(argv[1], "-r") == 0){
      fd = open(argv[2], O_RDWR);
      repair++;
    } else {
      printUsage();
    }
  } else if (argc == 2){
    fd = open(argv[1], O_RDONLY);
  } else {
    printUsage();
  }
  if(fd < 0){
    fprintf(stderr,"image not found.\n");
    exit(1);
  }

  int rc;
  struct stat sbuf;
  rc = fstat(fd, &sbuf);
  assert(rc == 0);

  img_ptr = mmap(NULL, sbuf.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
  if(mmap == MAP_FAILED){
    exit(1);
  }

  struct superblock *sb;
  sb = (struct superblock *) (img_ptr + BSIZE);

  struct dinode *dip = (struct dinode *) (img_ptr + 2*BSIZE);
  struct dinode *inp = (struct dinode *) (img_ptr + 2*BSIZE);
  int indin[sb->nblocks];
  int dirctpl[sb->nblocks];
  int count = 0;
  int allocblock[sb->nblocks];
  int freeblock[sb->nblocks];
  int alloc_count = 0;
  int free_count = 0;
  int datablock[sb->nblocks];
  int index = 0;
  int ininuse[sb->ninodes];
  int duplicate = 0;
  int refnum[sb->ninodes]; //each index(=file) contains number of references
  ushort lost_found;
//Setting all refs that will be updated as traverse through the inodes
  for(int i = 0; i < sb->ninodes; i++){
    refnum[i] = 0;
  }

//------------Creating array of Referenced number and inuse Inodes-------------
  for(int i = 0; i < sb->ninodes; i++){
    if(inp->type == 1){
      for(int j = 0; j < NDIRECT + 1; j++){
        if(j == 0){
          if(inp->addrs[j] != 0){
            struct dirent *entry = (struct dirent*) ((inp->addrs[j] * BSIZE) + img_ptr);
            for(int dirn = 2; dirn < BSIZE/sizeof(struct dirent); dirn++){
              if((entry+dirn)->inum != 0){
                refnum[(entry+dirn)->inum]++;
                if(strcmp((entry+dirn)->name, "lost_found") == 0){
                  lost_found = (entry+dirn)->inum;
                }
                if(index == 0) {
                  ininuse[index] = (entry+dirn)->inum;
                  index++;
                } else {
                  for(int k = 0; k < index; k++){
                    if((entry+dirn)->inum == ininuse[k]){
                      duplicate++;
                    }
                  }
                  if(duplicate == 0){
                    ininuse[index] = (entry+dirn)->inum;
                    index++;
                  }
                  duplicate = 0;
                }
              }
            }
          }
        }
        if(j < NDIRECT && j > 0){
          if(inp->addrs[j] != 0){
            struct dirent *entry = (struct dirent*) ((inp->addrs[j] * BSIZE) + img_ptr);
            for(int dirn = 0; dirn < BSIZE/sizeof(struct dirent); dirn++){
              if((entry+dirn)->inum != 0){
                refnum[(entry+dirn)->inum]++;
                if(strcmp((entry+dirn)->name, "lost_found") == 0){
                  lost_found = (entry+dirn)->inum;
                }
                if(index == 0) {
                  ininuse[index] = (entry+dirn)->inum;
                  index++;
                } else {
                  for(int k = 0; k < index; k++){
                    if((entry+dirn)->inum == ininuse[k]){
                      duplicate++;
                    }
                  }
                  if(duplicate == 0){
                    ininuse[index] = (entry+dirn)->inum;
                    index++;
                  }
                  duplicate = 0;
                }
              }
            }
          }
        } 
        if(j == 12) {
          uint *indino = (uint*)((inp->addrs[j] * BSIZE) + img_ptr);
          for(int k = 0; k < NINDIRECT; k++){
            if(*(indino + k) != 0){
              struct dirent *entry = (struct dirent*)((*(indino + k) * BSIZE) + img_ptr);
              for(int dirn = 0; dirn < BSIZE/sizeof(struct dirent); dirn++){
                if((entry+dirn)->inum != 0){
                  refnum[(entry+dirn)->inum]++;
                  if(strcmp((entry+dirn)->name, "lost_found") == 0){
                    lost_found = (entry+dirn)->inum;
                  } 
                  if(index == 0) {
                    ininuse[index] = (entry+dirn)->inum;
                    index++;
                  } else {
                    for(int k = 0; k < index; k++){
                      if(ininuse[index] == ininuse[k]){
                        duplicate++;
                      }
                    }
                    if(duplicate == 0){
                      ininuse[index] = (entry+dirn)->inum;
                      index++;
                    }
                    duplicate = 0;
                  }
                }
              }
            }
          }
        }
      }
    }
    inp++;  
  }
  refnum[1] = 1; //the root will always have reference count of 1
  for(int i = 0; i < sb->ninodes; i++){
    //printf("%d: %d\n",i, refnum[i]);
  }

////------------End of Creating array--------------------------------
//  //Check if inuse inode array is well made
//  inp = (struct dinode *) (img_ptr + 2*BSIZE);
//  for(int i = 0; i < index; i++){
//    printf("inodes in use: %d %d\n", ininuse[i], (inp+ininuse[i])->size);
//  }

////-------------Now Check------------------------
  inp = (struct dinode *) (img_ptr + 2*BSIZE);
  for(int i = 0; i < index; i++){
    inp += ininuse[i]; 
    if(inp->type == 0){
      
      fprintf(stderr,"ERROR: inode referred to in directory but marked free.\n");
      exit(1);
    }
    inp = (struct dinode *) (img_ptr + 2*BSIZE); 
  }
////---------------------------------

  int unuse_count = 0;
////--------Creaing unused inode array---------
  for(int i = 0; i < sb->ninodes; i++){
    for(int j = 0; j < index; j++){
      if(i == ininuse[j]){
        duplicate++;
      } 
    }
    if(duplicate == 0){
      unuse_count++;
    }
    duplicate = 0;
  }  
//Check if inuse inode array is well made
//  for(int i = 0; i < unuse_count; i++){
//      printf("inodes not in use: %d size %d\n", inunuse[i], (inp+inunuse[i])->size);
//  }
////----------------------Check------------------------------


  index = 0;
  inp = (struct dinode *) (img_ptr + 2*BSIZE);

  for(int i = sb->size - sb->nblocks; i < sb->size; i ++){
    datablock[index] = i;
    index++;
  }
//-----------------Direct Address Check ----------------------
  for(int i = 0; i < sb->ninodes; i++){
    if(inp->type < 4 && inp->type > 0){
      for(int j = 0; j < NDIRECT; j++){
        if(inp->addrs[j] != 0){
          dirctpl[count] = inp->addrs[j];
          allocblock[alloc_count] = inp->addrs[j];
          alloc_count++;
          for(int k = 0; k < count; k++){
            if(dirctpl[k] == dirctpl[count]){
              fprintf(stderr, "ERROR: direct address used more than once.\n"); 
              exit(1);
            }
          }
          count++;
          
        }
      }
    }
    inp++;
  }
//----------------Inirect Address Check Begins----------------------
  inp = (struct dinode *) (img_ptr + 2*BSIZE);
  count = 0;
  for(int i = 0; i < sb->ninodes; i++){
    if(inp->type < 4 && inp->type > 0){
      if(inp->addrs[NDIRECT] != 0){
        indin[count] = inp->addrs[NDIRECT];
        allocblock[alloc_count] = inp->addrs[NDIRECT];
        alloc_count++;
        for(int j = 0; j < count; j++){
          if(indin[j] == indin[count]){
            fprintf(stderr, "ERROR: indirect address used more than once.\n");
            exit(1);
          }
        }
        count++;
        uint *indino = (uint*)((inp->addrs[NDIRECT] * BSIZE) + img_ptr);
        for(int k = 0; k < NINDIRECT; k++){
          if(*(indino+k) != 0){
            indin[count] = *(indino+k);
            allocblock[alloc_count] = *(indino+k);
            alloc_count++;
            for(int j = 0; j < count; j++){
              if(indin[j] == indin[count]){
                fprintf(stderr, "ERROR: indirect address used more than once.\n");
                exit(1);
              }
            }
            count++;
          }
        }
      }
      
    }
    inp++;
  }

//-----------------------Address Check End----------------------

//Check if used inode array is well created
//  for(int i = 0; i < alloc_count; i++){
//    printf("Used block: %d\n", allocblock[i]);
//  }

  for(int i = 0; i < sb->nblocks; i++){
    for(int j = 0; j < alloc_count; j++){
      if(datablock[i] == allocblock[j]){
        datablock[i] = 0;
      }
    }
  }
  for(int i = 0; i < sb->nblocks; i++){
    if(datablock[i] != 0){
      freeblock[free_count] = datablock[i];
      free_count++;
    }
  }

  inp = (struct dinode *) (img_ptr + 2*BSIZE); 

  for(int i = 0; i < sb->ninodes; i++){

//-----------Root Check Begins----------------
    if(i == 1){
      struct dirent *entry = (struct dirent*)((dip->addrs[0] * BSIZE) + img_ptr);
      //Metadata: First entry = "." Second entry = ".."
      if(dip->type != 1){
        fprintf(stderr, "ERROR: root directory does not exist.\n");
        exit(1);
      }
      if((strcmp(entry->name, ".") != 0)){
        fprintf(stderr, "ERROR: directory not properly formatted.\n");
        exit(1);
      }
      if(entry->inum != i){
        fprintf(stderr, "ERROR:  directory not properly formatted.\n");
        exit(1);
      }
      if((entry+1)->inum != 1){
        fprintf(stderr, "ERROR: root directory does not exist.\n");
        exit(1);
      }
      if((strcmp((entry + 1)->name, "..") != 0)){
        fprintf(stderr, "ERROR: directory not properly formatted.\n");
        exit(1);
      }
      
    }
//-----------Root Check Ended-----------

//===========Check Block Addresses & Bitmap=============
    if(dip->type > 3 ){
      fprintf(stderr, "ERROR: bad inode.\n");
      exit(1);
    } else if (dip->type != 0){
      for(int j = 0; j < NDIRECT + 1; j++){
        int bitmapn = BBLOCK(dip->addrs[j], sb->ninodes); //Calculating the block number of bitmap
        char* bitmapaddr = (char*)((bitmapn * BSIZE) + img_ptr);
        if(j < NDIRECT){//Direct
          if(dip->addrs[j] != 0){ 
            if(dip->addrs[j] >= sb->size || dip->addrs[j] < sb->size - sb->nblocks){
              fprintf(stderr, "ERROR: bad direct address in inode.\n");
              exit(1);
            }
            if((bitmapaddr[dip->addrs[j]/8] & (0x1 << (dip->addrs[j]%8))) == 0){
              fprintf(stderr, "ERROR: address used by inode but marked free in bitmap.\n");
              exit(1);
            }
          } 
        }
        if(j == NDIRECT){ //Indirect
          if(dip->addrs[j] != 0){
            if(dip->addrs[j] >= sb->size || dip->addrs[j] < sb->size - sb->nblocks){ //Indirect Pointer
              fprintf(stderr, "ERROR: bad indirect address in inode.\n");
              exit(1);
            } else { 
              uint *indino = (uint*)((dip->addrs[j] * BSIZE) + img_ptr);
              for(int k = 0; k < NINDIRECT; k++){
                if(*(indino + k) != 0){
                  if(*(indino + k) >= sb->size || *(indino + k) < sb->size - sb->nblocks){ //Indirect Addresses
                    fprintf(stderr, "ERROR: bad indirect address in inode.\n");
                    exit(1);
                  }
                  if((bitmapaddr[*(indino + k)/8] & (0x1 << (*(indino + k)%8))) == 0){
                    fprintf(stderr, "ERROR: address used by inode but marked free in bitmap.\n");
                    exit(1);
                  }
                }
              }
            }
          }
        }
      }
//===========Check Blocks & Bitmap Ended=============

      if(dip->type == 1 && i > 1){	
        for(int j = 0; j < NDIRECT + 1; j++){

//-----------Directory Check Begins-----------------------
          struct dirent *entry = (struct dirent*)((dip->addrs[j] * BSIZE) + img_ptr); 
          if(j == 0){
            if(strcmp(entry->name, ".") != 0){
              fprintf(stderr, "ERROR: directory not properly formatted.\n");
              exit(1);
            
            }
            if(entry->inum != i){
              fprintf(stderr, "ERROR: directory not properly formatted.\n");
              exit(1);
            }
            if (strcmp((entry+1)->name, "..") != 0){ 
              fprintf(stderr, "ERROR: directory not properly formatted.\n");
              exit(1);
            }

          }
        }
//------------Directory Check Ended---------------------
      }
    }  
    dip++;
  }

//-----------Used Bitmap Check------------------
  for(int i = 0; i < free_count; i++){
    int bitmapn = BBLOCK(freeblock[i], sb->ninodes);
    char* bitmapaddr = (char*)((bitmapn * BSIZE) + img_ptr);
    if((bitmapaddr[freeblock[i]/8] & (0x1 << (freeblock[i]%8))) != 0){
      fprintf(stderr, "ERROR: bitmap marks block in use but it is not in use.\n"); 
      exit(1);
    }
  }
//-----------Used Bitmap End------------------


  struct dinode *lofo = inp + lost_found;
  int lostcount = 0;
//===========Reference Counts & Used Inode Check=============
  inp = (struct dinode *) (img_ptr + 2*BSIZE);
  for(int i = 0; i < sb->ninodes; i++){
    if(inp->type > 0 && inp->type < 4){ //
      if(repair == 0){
        if(refnum[i] == 0){
          
            //printf("%d\n", i);
            fprintf(stderr, "ERROR: inode marked use but not found in a directory.\n");
            exit(1);
        }
        if(inp->type == 2){
          if(refnum[i] != inp->nlink){
            fprintf(stderr, "ERROR: bad reference count for file.\n"); //BADREF2 Doesn't print out error
            exit(1);
          }
        }
        if(inp->type == 1){
          if(refnum[i] > 1){
            fprintf(stderr, "ERROR: directory appears more than once in file system.\n");
            exit(1);
          }
        }
      } else if (repair > 0){
        if(refnum[i] == 0){
          repairx(fd, i, lofo, inp, lostcount);
          lostcount++;
        }
      }
    }
    inp++;
  }
//===========Reference Counts & Used Inode End=============

  dip = (struct dinode *) (img_ptr + 2*BSIZE);
  int parent[sb->ninodes]; //stores parent inodes for current inode
  for(int i = 0; i < sb->ninodes; i++){ //All inodes that are not used will store parent 0
    parent[i] = 0;
  }
  parent[1] = 1; //parent of root is one
  //Creating the lists of parents. Ex) Parent[Child Inode] = Parent Inode
  for(int i = 0; i < sb->ninodes; i++){
    if(dip->type == 1){
      for(int k = 0; k < NDIRECT + 1; k++){
        if(k == 0){
          struct dirent *entry = (struct dirent*)((dip->addrs[0]*BSIZE) + img_ptr);
          for(int j = 2; j < BSIZE/sizeof(struct dirent); j++){
            if((entry+j)->inum != 0){
              parent[(entry+j)->inum] = i;
            }
          }
        } else if (k < NDIRECT){
          struct dirent *entry = (struct dirent*)((dip->addrs[k]*BSIZE) + img_ptr);
          for(int j = 0; j < BSIZE/sizeof(struct dirent); j++){
            if((entry+j)->inum != 0){
              parent[(entry+j)->inum] = i;
            }
          }
        } else {
          uint *indino = (uint*)((dip->addrs[NDIRECT] * BSIZE) + img_ptr);
          for(int j = 0; j < NINDIRECT; j++){
            struct dirent *entry = (struct dirent*)(*(indino + j)*BSIZE + img_ptr);
            for(int l = 0; l < BSIZE/sizeof(struct dirent); l++){
              if((entry+l)->inum != 0){
                parent[(entry+l)->inum] = i;
              }
            }
          }
        }
      }
    }
    dip++;
  }

//Check If parent list is well Created=============
//  for(int i = 0; i < sb->ninodes; i++){
//    if(parent[i] != 0){
//      printf("Parent of inode %d is %d\n",i, parent[i]);
//    }
//  }
//=================================================

//=============Compare My Parent list to Entry's Parent inode=============
  dip = (struct dinode *) (img_ptr + 2*BSIZE);
  for(int i = 0; i < sb->ninodes; i++){
    if(dip->type == 1){
      struct dirent *entry = (struct dirent*)(dip->addrs[0]*BSIZE + img_ptr);
      if(parent[i] != (entry+1)->inum){
        fprintf(stderr, "ERROR: parent directory mismatch.\n");
        exit(1);
      }
    }
    dip++;
  }
//=============Compare My Parent list to Entry's Parent inode=============

//=============Check if there is dangling directories=====================
  for(int i = 0; i < sb->ninodes; i++){
    if(parent[i] != 0){
      int inode = i;
      int dirlist[sb->ninodes];
      int dir_count = 0;
      while(parent[inode] != 1){
        dirlist[dir_count] = parent[inode];
        for(int j = 0; j < dir_count; j++){
          if(dirlist[dir_count] == dirlist[j]){
            fprintf(stderr, "ERROR: inaccessible directory exists.\n");
            exit(1);
          }
        }
        dir_count++;
        inode = parent[inode];
      }
    }
  }
  return 0;
}
