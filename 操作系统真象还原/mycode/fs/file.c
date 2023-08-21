#include "file.h"
#include "fs.h"
#include "super_block.h"
#include "inode.h"
#include "stdio-kernel.h"
#include "memory.h"
#include "debug.h"
#include "interrupt.h"
#include "string.h"
#include "thread.h"
#include "global.h"

#define DEFAULT_SECS 1

//文件表，也就是文件结构数组
struct file file_table[MAX_FILE_OPEN];

//从文件表file_table中获取一个空闲位，成功返回下标，失败返回-1
int32_t get_free_slot_in_global(void) {
  uint32_t fd_idx = 3;
  while (fd_idx < MAX_FILE_OPEN) {    //遍历file_table，找出fd_inode为null的数组元素
    if (file_table[fd_idx].fd_inode == NULL) {
      break;
    }
    fd_idx++;
  }
  if (fd_idx == MAX_FILE_OPEN) {
    printk("exceed max open files\n");
    return -1;
  }
  return fd_idx;
}

//将全局描述符下标安装到进程或线程自己的文件描述符数组fd_table中，成功返回下标，失败返回-1
//接受1个参数，全局描述符下标globa_fd_idx，也就是数组file_table的下标。函数功能是将globa_fd_idx
//安装到进程或线程自己的文件描述符数组fd_table中，成功则返回fd_table中空位的下标，失败则返回−1
int32_t pcb_fd_install(int32_t globa_fd_idx) {
  struct task_struct *cur = running_thread();
  uint8_t local_fd_idx = 3;     //跨过stdin,stdout,stderr
  while (local_fd_idx < MAX_FILES_OPEN_PER_PROC) {
    if (cur->fd_table[local_fd_idx] == -1) {    //-1表示free_slot，可用
      cur->fd_table[local_fd_idx] = globa_fd_idx;
      break;
    }
    local_fd_idx++;
  }
  if (local_fd_idx == MAX_FILES_OPEN_PER_PROC) {
    printk("exceed max open files_per_proc\n");
    return -1;
  }
  return local_fd_idx;
}

//分配一个i结点，返回i结点号
//接受1个参数，分区part，功能是分配一个i结点，返回i结点号
int32_t inode_bitmap_alloc(struct partition *part) {
  int32_t bit_idx = bitmap_scan(&part->inode_bitmap, 1);
  if (bit_idx == -1) {
    return -1;
  }
  bitmap_set(&part->inode_bitmap, bit_idx, 1);
  return bit_idx;
}

//分配1个扇区，返回其扇区地址
//接受1个参数，分区part，功能是分配1个扇区，返回其扇区地址
int32_t block_bitmap_alloc(struct partition *part) {
  int32_t bit_idx = bitmap_scan(&part->block_bitmap, 1);
  if (bit_idx == -1) {
    return -1;
  }
  bitmap_set(&part->block_bitmap, bit_idx, 1);
  //和inode_bitmap_alloc不同，此处返回的不是位图索引，而是具体可用的扇区地址
  return (part->sb->data_start_lba + bit_idx);
}

//将内存中bitmap第bit_idx位所在的512字节同步到硬盘
//接受3个参数，分区part、位索引bit_idx、位图类型btmp_type，
//功能是将内存中bitmap第bit_idx位所在的512字节同步到硬盘
void bitmap_sync(struct partition *part, uint32_t bit_idx, uint8_t btmp_type) {
  uint32_t off_sec = bit_idx / 4096;        //本i结点索引相对于位图的扇区偏移量
  uint32_t off_size = off_sec * BLOCK_SIZE; //本i结点索引相对于位图的字节偏移量

  uint32_t sec_lba;     //位图的扇区地址
  uint8_t *bitmap_off;  //字节偏移量

  //需要被同步到硬盘的位图只有inode_bitmap和block_bitmap
  switch(btmp_type) {
    case INODE_BITMAP:
      sec_lba = part->sb->inode_bitmap_lba + off_sec;   //位图的扇区地址
      bitmap_off = part->inode_bitmap.bits + off_size;  //字节偏移量
      break;
    case BLOCK_BITMAP:
      sec_lba = part->sb->block_bitmap_lba + off_sec;   //位图的扇区地址
      bitmap_off = part->block_bitmap.bits + off_size;  //字节偏移量
      break;
  }
  ide_write(part->my_disk, sec_lba, bitmap_off, 1);
}