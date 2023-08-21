#include "inode.h"
#include "fs.h"
#include "file.h"
#include "global.h"
#include "debug.h"
#include "memory.h"
#include "interrupt.h"
#include "list.h"
#include "stdio-kernel.h"
#include "string.h"
#include "super_block.h"

//用来存储inode位置
//用于记录inode所在的扇区地址及在扇区内的偏移量，也就是用于定位inode在磁盘上的位置
struct inode_position {
  bool two_sec;       //inode是否跨扇区，即占用2个扇区的情况
  uint32_t sec_lba;   //inode所在的扇区号
  uint32_t off_size;  //inode在扇区内的字节偏移量
};

//建文件描述符的过程就是逐层在这三个数据结构中找空位，在该空位填充好数据后返回该位置的地址，比如：
//（1）在全局的inode队列中新建一inode（这肯定是在空位置处新建），然后返回该inode地址。
//（2）在全局的文件表中的找一空位，在该位置填充文件结构，使其fd_inode指向上一步中返回的inode
//地址，然后返回本文件结构在文件表中的下标值。
//（3）在PCB中的文件描述符数组中找一空位，使该位置的值指向上一步中返回的文件结构下标，
//并返回本文件描述符在文件描述符数组中的下标值。

//获取inode所在的扇区和扇区内的偏移量
//接受3个参数，分区part、inode编号inode_no及inode_pos，inode_pos类型是上面提到的struct inode_position，
//用于记录inode在硬盘上的位置，函数功能是定位inode所在的扇区和扇区内的偏移量，将其写入inode_pos中
static void inode_locate(struct partition *part, uint32_t inode_no, struct inode_position *inode_pos) {
  //inode_table在硬盘上是连续的
  ASSERT(inode_no < 4096);
  uint32_t inode_table_lba = part->sb->inode_table_lba;   //获取inode_table的扇区地址

  uint32_t inode_size = sizeof(struct inode);
  //第inode_no号I结点相对于inode_table_lba的字节偏移量
  uint32_t off_size = inode_no * inode_size;
  //第inode_no号I结点相对于inode_table_lba的扇区偏移量
  uint32_t off_sec = off_size / 512;    //偏移扇区地址
  //待查找的inode所在扇区中的起始地址
  uint32_t off_size_in_sec = off_size % 512;    //在扇区中的偏移字节，off_sec是相对于inode_table_lba的扇区偏移量

  //判断此i结点是否跨越2个扇区
  uint32_t left_in_sec = 512 - off_size_in_sec;
  if (left_in_sec < inode_size) {
    //若扇区内剩下的空间不足以容纳一个inode，必然是I结点跨越了2个扇区
    inode_pos->two_sec = true;
  } else {    //否则，所查找的inode未跨扇区
    inode_pos->two_sec = false;
  }
  inode_pos->sec_lba = inode_table_lba + off_sec; //inode的绝对扇区地址
  inode_pos->off_size = off_size_in_sec;
}

//将inode写入到分区part
//接受3个参数，分区part、待同步的inode指针、操作缓冲区io_buf，函数功能是将inode写入到磁盘分区part
void inode_sync(struct partition *part, struct inode *inode, void *io_buf) {
  //io_buf是用于硬盘io的缓冲区
  uint8_t inode_no = inode->i_no;
  struct inode_position inode_pos;
  inode_locate(part, inode_no, &inode_pos);
  //inode位置信息会存入inode_pos
  ASSERT(inode_pos.sec_lba <= (part->start_lba + part->sec_cnt));

  //硬盘中的inode中的成员inode_tag和i_open_cnts是不需要的，它们只在内存中记录链表位置和被多少进程共享
  struct inode pur_inode;
  memcpy(&pur_inode, inode, sizeof(struct inode));

  //以下inode的三个成员只存在于内存中，现在将inode同步到硬盘，清掉这三项即可
  pur_inode.i_open_cnts = 0;
  //置为false，以保证在硬盘中读出时为可写
  pur_inode.write_deny = false;
  pur_inode.inode_tag.prev = pur_inode.inode_tag.next = NULL;

  char *inode_buf = (char *)io_buf;
  if (inode_pos.two_sec) {    //判断是否跨扇区
    //若是跨了两个扇区，就要读出两个扇区再写入两个扇区
    //读写硬盘是以扇区为单位，若写入的数据小于一扇区，要将原硬盘上的内容先读出来再和新数据拼成一扇区后再写入
    ide_read(part->my_disk, inode_pos.sec_lba, inode_buf, 2); //inode在format中写入硬盘时是连续写入的，所以读入2块扇区
    //开始将待写入的inode拼入到这2个扇区中的相应位置
    memcpy((inode_buf + inode_pos.off_size), &pur_inode, sizeof(struct inode));

    //将拼接好的数据再写入磁盘
    ide_write(part->my_disk, inode_pos.sec_lba, inode_buf, 2);
  } else {
    ide_read(part->my_disk, inode_pos.sec_lba, inode_buf, 1);
    memcpy((inode_buf + inode_pos.off_size), &pur_inode, sizeof(struct inode));
    ide_write(part->my_disk, inode_pos.sec_lba, inode_buf, 1);
  }
}

//根i结点号返回相应的i结点
//接受两个参数，分区part及inode编号inode_no，函数功能是根据inode_no返回相应的i结点指针。
struct inode *inode_open(struct partition *part, uint32_t inode_no) {
  //先在已打开的inode链表中找inode，此链表是为提速创建的缓冲区
  struct list_elem *elem = part->open_inodes.head.next;
  struct inode *inode_found;
  while (elem != &part->open_inodes.tail) {   //遍历part->open_inodes
    inode_found = elem2entry(struct inode, inode_tag, elem);
    if (inode_found->i_no == inode_no) {
      inode_found->i_open_cnts++;
      return inode_found;
    }
    elem = elem->next;
  }
  //由于open_inodes链表中找不到，下面从硬盘上读入此inode并加入到此链表
  struct inode_position inode_pos;

  //inode位置信息会存入inode_pos，包括inode所在扇区地址和扇区内的字节偏移量
  inode_locate(part, inode_no, &inode_pos);
  
  //为使通过sys_malloc创建的新inode被所有任务共享，
  //需要将inode置于内核空间，故需要临时cur_pbc->pgdir置为NULL
  struct task_struct *cur = running_thread();
  uint32_t *cur_pagedir_bak = cur->pgdir;
  cur->pgdir = NULL;
  //以上三行代码完成后下面分配的内存将位于内核区
  inode_found = (struct inode *)sys_malloc(sizeof(struct inode));
  //恢复pgdir
  cur->pgdir = cur_pagedir_bak;

  char *inode_buf;
  if (inode_pos.two_sec) {    //考虑跨扇区的情况
    inode_buf = (char *)sys_malloc(1024);
    //i结点表是被partition_format函数连续写入扇区的，所以下面可以连续读出来
    ide_read(part->my_disk, inode_pos.sec_lba, inode_buf, 2);
  } else {
    inode_buf = (char *)sys_malloc(512);
    ide_read(part->my_disk, inode_pos.sec_lba, inode_buf, 1);
  }
  memcpy(inode_found, inode_buf + inode_pos.off_size, sizeof(struct inode));

  //因为一会很可能要用到此inode，故将其插入到队首便于提前检索到
  list_push(&part->open_inodes, &inode_found->inode_tag);
  inode_found->i_open_cnts = 1;

  sys_free(inode_buf);
  return inode_found;
}

//关闭inode或减少inode的打开数
//接受1个参数，inode指针inode，功能是关闭inode。
void inode_close(struct inode *inode) {
  //若没有进程再打开此文件，将此inode去掉并释放空间
  enum intr_status old_status = intr_disable();
  if (--inode->i_open_cnts == 0) {
    list_remove(&inode->inode_tag);   //将I结点从part->open_inodes中去掉
    //inode_open时为实现inode被所有进程共享，已经在sys_malloc为inode分配了内核空间，
    //释放inode时也要确保释放的是内核内存池
    struct task_struct *cur = running_thread();
    uint32_t *cur_pagedir_bak = cur->pgdir;
    cur->pgdir = NULL;
    sys_free(inode);
    cur->pgdir = cur_pagedir_bak;
  }
  intr_set_status(old_status);
}

//初始化new_inode
void inode_init(uint32_t inode_no, struct inode *new_inode) {
  new_inode->i_no = inode_no;
  new_inode->i_size = 0;
  new_inode->i_open_cnts = 0;
  new_inode->write_deny = false;
  
  //初始化块索引数组i_sector
  uint8_t sec_idx = 0;
  while (sec_idx < 13) {
    //i_sectors[12]为一级间接块地址
    new_inode->i_sectors[sec_idx] = 0;
    sec_idx++;
  }
}