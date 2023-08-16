#ifndef __DEVICE_IDE_H
#define __DEVICE_IDE_H
#include "stdint.h"
#include "sync.h"
#include "bitmap.h"

//分区结构
struct partition {
  uint32_t start_lba;		          //分区的起始扇区
  uint32_t sec_cnt;		            //分区的容量扇区数
  //一个硬盘有很多分区，因此成员my_disk表示此分区属于哪个硬盘
  struct disk* my_disk;	          //分区所属的硬盘
  //本分区的标记，将来会将分区汇总到队列中
  struct list_elem part_tag;	    //用于队列中的标记
  char name[8];		                //分区名称，如sda1、sda2
  struct super_block* sb;	        //本分区的超级块  超级块指针，此处只是用来占个位置
  //为了减少对低速磁盘的访问次数，文件系统通常以多个扇区为单位来读写磁盘，这多个扇区就是块。
  //block_bitmap是块位图，用来管理本分区所有块，为了简单，咱们的块大小是由1个扇区组成的
  struct bitmap block_bitmap;	    //块位图
  //inode_bitmap是i结点管理位图
  struct bitmap inode_bitmap;	    //i结点位图
  //open_inodes是分区所打开的inode队列
  struct list open_inodes;	      //本分区打开的i结点队列
};

//硬盘结构
struct disk {
  char name[8];			                //本硬盘的名称，如sda、sdb等
  //一个通道上有两块硬盘，所以my_channel用于指定本硬盘所属的通道
  struct ide_channel* my_channel;   //此块硬盘归属于哪个ide通道
  uint8_t dev_no;			              //本硬盘是主0还是从1
  struct partition prim_parts[4];   //本硬盘中的主分区数量，主分区顶多是4个
  struct partition logic_parts[8];  //逻辑分区的数量，逻辑分区数量无限,但总得有个支持的上限,那就支持8个
};

//此结构表示ide通道，也就是ata通道
struct ide_channel {
  char name[8];		              //本ata通道名称，如ata0或ide0
  /*
  port_base是本通道的端口基址，咱们这里只处理两个通道的主板，每个通道的端口范围是不一样的，
  通道1（Primary通道）的命令块寄存器端口范围是0x1F0～0x1F7，控制块寄存器
  端口是0x3F6，通道2（Secondary通道）命令块寄存器端口范围是0x170～0x177，控制块寄存器端口是
  0x376。通道1的端口可以以0x1F0为基数，其命令块寄存器端口在此基数上分别加上0～7就可以了，
  控制块寄存器端口在此基数上加上0x206，同理，通道2的基数就是0x170。
  */
  uint16_t port_base;           //本通道的起始端口号
  //本通道所用的中断号，在硬盘的中断处理程序中要根据中断号来判断在哪个通道中操作
  uint8_t irq_no;		            //本通道所用的中断号
  //用来实现通道的互斥，因为1个通道中有主、从两块硬盘,通道中的两个硬盘也只能共用同一个中断
  //中断发生时，中断处理程序是如何区分中断信号来
  //自哪一个硬盘呢？还真不知道怎样区分，所以一次只允许通道中的1个硬盘操作
  struct lock lock;		          //通道锁
  //驱动程序向硬盘发完命令后等待来自硬盘的中断，中断处理程序中会通过此成员来判断此次的中断
  //是否是因为之前的硬盘操作命令引起的，如果是，则进行下一步动作，如获取数据等。
  bool expecting_intr;	        //表示本通道正等待硬盘中断
  //disk_done是个信号量，作用是：驱动程序向硬盘发送命令后，在等待硬盘工作期间可以通过此信号量阻塞自己，
  //避免干等着浪费CPU。等硬盘工作完成后会发出中断，中断处理程序通过此信号量将硬盘驱动程序唤醒。
  struct semaphore disk_done;	  //用于阻塞、唤醒驱动程序
  struct disk devices[2];	      //表示一个通道上连接两个硬盘，一主一从
};
void intr_hd_handler(uint8_t irq_no);
void ide_init(void);
extern uint8_t channel_cnt;
extern struct ide_channel channels[];
extern struct list partition_list;
void ide_read(struct disk* hd, uint32_t lba, void* buf, uint32_t sec_cnt);
void ide_write(struct disk* hd, uint32_t lba, void* buf, uint32_t sec_cnt);
#endif