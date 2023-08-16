#ifndef __FS_DIR_H
#define __FS_DIR_H
#include "stdint.h"
#include "inode.h"
#include "ide.h"
#include "global.h"

#define MAX_FILE_NAME_LEN  16	  //最大文件名长度

//目录结构
//它并不在磁盘上存在，只用于与目录相关的操作时，在内存中创建的结构，用过之后就释放了，不会回写到磁盘中
struct dir {
  struct inode *inode;    //用于指向内存中inode，该inode必然是在“已打开的inode队列”
  //用于遍历目录时记录“游标”在目录中的偏移，也就是目录项的偏移量，dir_pos大小应为目录项大小的整数倍
  uint32_t dir_pos;       //记录在目录内的偏移
  //用于目录的数据缓存，如读取目录时，用来存储返回的目录项
  uint8_t dir_buf[512];   //目录的数据缓存
};

//目录项结构
struct dir_entry {
  char filename[MAX_FILE_NAME_LEN];   //普通文件或目录名称
  uint32_t i_no;                      //普通文件或目录对应的inode编号
  enum file_types f_type;             //文件类型
};

#endif