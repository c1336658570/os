#ifndef __FS_INODE_H
#define __FS_INODE_H
#include "stdint.h"
#include "list.h"
#include "ide.h"

//inode结构
struct inode {
  uint32_t i_no;    //inode编号，它是在inode数组中的下标
  //当此inode是文件时，i_size是指文件大小,若此inode是目录，i_size是指该目录下所有目录项大小之和
  uint32_t i_size;  //此inode指向的文件的大小（以字节为单位的大小）
  uint32_t i_open_cnts; //记录此文件被打开的次数
  bool write_deny;      //写文件不能并行，进程写文件前检查此标识，用于限制文件的并行写操作
  //i_sectors[0-11]是直接块，i_sectors[12]用来存储一级间接块指针
  //扇区大小是512字节，并且块地址用4字节来表示，因此咱们支持的一级间接块数量是128个，总共支持128+12=140个块（扇区）
  uint32_t i_sectors[13];   //数据块的指针，块大小就是1扇区。直接把块数组命名为i_sector（而不是像ext2中的i_block）
  struct list_elem inode_tag; //此inode的标识，用于加入“已打开的inode列表”，目的如下
  //由于inode是从硬盘上保存的，文件被打开时，肯定是先要从硬盘上载入其inode，但硬盘比较慢，
  //为了避免下次再打开该文件时还要从硬盘上重复载入inode，应该在该文件第一次被打开时就将其inode加入到内存缓存中，
  //每次打开一个文件时，先在此缓冲中查找相关的inode，如果有就直接使用，否则再从硬盘上读取inode，然后再加入此缓存
};

struct inode* inode_open(struct partition* part, uint32_t inode_no);
void inode_sync(struct partition* part, struct inode* inode, void* io_buf);
void inode_init(uint32_t inode_no, struct inode* new_inode);
void inode_close(struct inode* inode);
void inode_release(struct partition* part, uint32_t inode_no);
void inode_delete(struct partition* part, uint32_t inode_no, void* io_buf);

#endif