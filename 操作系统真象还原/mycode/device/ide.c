#include "ide.h"
#include "sync.h"
#include "stdio.h"
#include "stdio-kernel.h"
#include "interrupt.h"
#include "memory.h"
#include "debug.h"
#include "string.h"
#include "io.h"
#include "timer.h"

//定义硬盘各寄存器的端口号
#define reg_data(channel)	        (channel->port_base + 0)  //read/write::data
#define reg_error(channel)	      (channel->port_base + 1)  //read:error write:features
#define reg_sect_cnt(channel)     (channel->port_base + 2)  //read/write:sector count
#define reg_lba_l(channel)	      (channel->port_base + 3)  //read/write:lba low
#define reg_lba_m(channel)	      (channel->port_base + 4)  //read/write:lba mid
#define reg_lba_h(channel)	      (channel->port_base + 5)  //read/write:lba high
#define reg_dev(channel)	        (channel->port_base + 6)  //read/write:device
#define reg_status(channel)	      (channel->port_base + 7)  //read:status
#define reg_cmd(channel)	        (reg_status(channel))     //write:command
#define reg_alt_status(channel)   (channel->port_base + 0x206)  //read:alternate status
#define reg_ctl(channel)	        reg_alt_status(channel)   //write:device control

//reg_alt_status寄存器的一些关键位  读alternate status（reg_alt_status）
//reg_alt_status寄存器的7-0位
//  7                     6                 5   4     3                   2   1     0
//  BSY(为1表示硬盘正忙)    DRDY(1表示设备就绪，等待指令)   DRQ(1表示硬盘准备号数据可以读出)  ERR(1表示有错误，错误见error寄存器)
#define BIT_STAT_BSY	 0x80	      //硬盘忙
#define BIT_STAT_DRDY	 0x40	      //驱动器准备好	 
#define BIT_STAT_DRQ	 0x8	      //数据传输准备好了

//device寄存器的一些关键位  写入到device control（reg_ctl）
//device寄存器的7-0位
//  7   6     5   4               3-0
//  1   MOD   1   DEV(主盘或从盘)   LBA的27 ~ 23
#define BIT_DEV_MBS	0xa0	        //第7位和第5位固定为1
#define BIT_DEV_LBA	0x40
#define BIT_DEV_DEV	0x10

//一些硬盘操作的指令  写入到command寄存器（reg_cmd）
#define CMD_IDENTIFY	      0xec	    //identify指令，即硬盘识别，用来获取硬盘的身份信息。
#define CMD_READ_SECTOR	    0x20      //读扇区指令
#define CMD_WRITE_SECTOR    0x30	    //写扇区指令

//定义可读写的最大扇区数,表示最大的lba地址，调试用的
#define max_lba ((80*1024*1024/512) - 1)	//只支持80MB硬盘

uint8_t channel_cnt;	            //用来表示机器上的ata通道数，按硬盘数计算的通道数
struct ide_channel channels[2];	  //有两个ide通道

//用于记录总扩展分区的起始lba，初始为0，partition_scan时以此为标记
//该变量有2作用，一是作为扫描分区表的标记，partition_scan若发现ext_lba_base为0便知道这是第一次扫描，
//因此初始为0。另外就是用于记录总扩展分区地址，那时肯定就不为0了
int32_t ext_lba_base = 0; //用在分区表扫描函数partition_scan中的
uint8_t p_no = 0, l_no = 0;   //用来记录硬盘主分区和逻辑分区的下标
struct list partition_list;   //分区队列  所有分区的列表

//构建一个16字节大小的结构体，用来存分区表项
struct partition_table_entry {
  uint8_t  bootable;		  //是否可引导	
  uint8_t  start_head;    //起始磁头号
  uint8_t  start_sec;		  //起始扇区号
  uint8_t  start_chs;		  //起始柱面号
  uint8_t  fs_type;		    //分区类型
  uint8_t  end_head;		  //结束磁头号
  uint8_t  end_sec;		    //结束扇区号
  uint8_t  end_chs;		    //结束柱面号
  //更需要关注的是下面这两项
  uint32_t start_lba;		  //本分区起始扇区的lba地址
  uint32_t sec_cnt;		    //本分区的扇区数目
} __attribute__ ((packed));	 //保证此结构是16字节大小
//__attribute__是gcc特有的关键字，用于告诉gcc在编译时需要做些“特殊处理”，packed就是“特殊处理”，
//意为压缩的，即不允许编译器为对齐而在此结构中填充空隙，从而保证结构partition_table_entry的大小是16字节

//引导扇区，mbr或ebr所在的扇区
struct boot_sector {
  uint8_t other[446];   //引导代码
  struct partition_table_entry partition_table[4];  //分区表中有4项，共64字节
  uint16_t signature;   //启动扇区的结束标志是0x55,0xaa
}__attribute__ ((packed));

//选择读写的硬盘
//接受一个参数，硬盘指针hd，功能是选择待操作的硬盘是主盘或从盘
static void select_disk(struct disk *hd) {
  uint8_t reg_device = BIT_DEV_MBS | BIT_DEV_LBA;
  if (hd->dev_no == 1) {    // 若是从盘就置DEV位为1
    reg_device |= BIT_DEV_DEV;
  }
  outb(reg_dev(hd->my_channel), reg_device);
}

//向硬盘控制器写入起始扇区地址及要读写的扇区数
//接受3个参数，硬盘指针hd、扇区起始地址lba、扇区数sec_cnt
//功能是向硬盘控制器写入起始扇区地址及要读写的扇区数
static void select_sector(struct disk *hd, uint32_t lba, uint8_t sec_cnt) {
  ASSERT(lba <= max_lba);
  struct ide_channel *channel = hd->my_channel;
  
  //写入要读写的扇区数
  outb(reg_sect_cnt(channel), sec_cnt); //如果sec_cnt为0，则表示写入256个扇区

  //写入lba地址，即扇区号
  //lba地址的低8位，不用单独取出低8位，outb函数中的汇编指令outb%b0，%w1会只用al
  outb(reg_lba_l(channel), lba);  
  outb(reg_lba_m(channel), lba >> 8);   //lba 地址的8～15位
  outb(reg_lba_h(channel), lba >> 16);  //lba地址的16～23位
  //因为lba地址的第24～27位要存储在device寄存器的0～3位，
  //无法单独写入这4位，所以在此处把device寄存器再重新写入一次
  outb(reg_dev(channel), BIT_DEV_MBS | BIT_DEV_LBA | (hd->dev_no == 1 ? BIT_DEV_DEV : 0) | lba >> 24);
}

//向通道channel发命令cmd
//接受2个参数，通道channel和硬盘操作命令cmd。函数功能是将通道channel发cmd命令。
static void cmd_out(struct ide_channel *channel, uint8_t cmd) {
  //只要向硬盘发出了命令便将此标记置为true，硬盘中断处理程序需要根据它来判断
  channel->expecting_intr = true;
  outb(reg_cmd(channel), cmd);
}

//硬盘读入sec_cnt个扇区的数据到buf
//接受3个参数，分别是待操作的硬盘hd、缓冲区buf、读取的扇区数sec_cnt
//功能是从硬盘hd中读入sec_cnt个扇区的数据到buf
static void read_from_sector(struct disk *hd, void *buf, uint8_t sec_cnt) {
  uint32_t size_in_byte;
  if (sec_cnt == 0) {
    //因为sec_cnt是8位变量，由主调函数将其赋值时，若为256则会将最高位的1丢掉变为0
    size_in_byte = 256 * 512;
  } else {
    size_in_byte = sec_cnt * 512;
  }
  insw(reg_data(hd->my_channel), buf, size_in_byte / 2);
}

//将buf中sec_cnt扇区的数据写入硬盘
//接受3个参数，硬盘hd、缓冲区buf、扇区数sec_cnt，功能是将buf中sec_cnt扇区的数据写入硬盘hd
static void write2sector(struct disk *hd, void *buf, uint8_t sec_cnt) {
  uint32_t size_in_byte;
  if (sec_cnt == 0) {
    //因为sec_cnt是8位变量，由主调函数将其赋值时，若为256则会将最高位的1丢掉变为0
    size_in_byte = 256 * 512;
  } else {
    size_in_byte = sec_cnt * 512;
  }
  outsw(reg_data(hd->my_channel), buf, size_in_byte / 2);
}

//等待30秒
//接受1个参数，硬盘hd，功能是等待硬盘30秒
static bool busy_wait(struct disk *hd) {
  struct ide_channel *channel = hd->my_channel;
  uint16_t time_limit = 30 * 1000;    //可以等待30000毫秒
  while (time_limit -= 10 >= 0) {
    if (!(inb(reg_status(channel)) & BIT_STAT_BSY)) { //判断status寄存器的BSY位是否为1，如果为1，则表示硬盘繁忙
      return (inb(reg_status(channel)) & BIT_STAT_DRQ); //返回其DRQ位的值，DRQ位为1表示硬盘已经准备好数据了
    } else {
      mtime_sleep(10);    //睡眠10ms
    }
  }
  return false;
}

//从硬盘读取sec_cnt个扇区到buf
//接受4个参数，硬盘hd、扇区地址lba、缓冲区buf、扇区数量sec_cnt
//功能是从硬盘hd的扇区地址lba处读取sec_cnt个扇区到buf
void ide_read(struct disk *hd, uint32_t lba, void *buf, uint32_t sec_cnt) {
  struct arena *arena = (struct arena*)((uint32_t)0x804b00c & 0xfffff000);
  ASSERT(lba <= max_lba);
  ASSERT(sec_cnt > 0);
  lock_acquire(&hd->my_channel->lock);

  //1先选择操作的硬盘
  select_disk(hd);

  //因为读写扇区数端口0x1f2及0x172是8位寄存器，故每次读
  //写最多是255个扇区（当写入端口值为0时，则写入256个扇区）
  //，所以当读写的端口数超过256时，必须拆分成多次读写操作
  //每当完成一个扇区的读写后，此寄存器的值便减1，所以当读写失败时，此端口包括尚未完成的扇区数。
  uint32_t secs_op;         //每次操作的扇区数
  uint32_t secs_done = 0;   //已完成的扇区数
  while (secs_done < sec_cnt) {
    if ((secs_done + 256) <= sec_cnt) {
      secs_op = 256;
    } else {
      secs_op = sec_cnt - secs_done;
    }

    //2写入待读入的扇区数和起始扇区号
    select_sector(hd, lba + secs_done, secs_op);

    //3执行的命令写入reg_cmd寄存器
    cmd_out(hd->my_channel, CMD_READ_SECTOR); //准备开始读数据
    
    //*********************阻塞自己的时机 ***********************
    //在硬盘已经开始工作（开始在内部读数据或写数据）后才能阻塞自己，
    //现在硬盘已经开始忙了，将自己阻塞，等待硬盘完成读操作后通过中断处理程序唤醒自己

    //对于读硬盘来说，驱动程序阻塞自己是在硬盘开始读扇区之后
    sema_down(&hd->my_channel->disk_done);

    //4检测硬盘状态是否可读
    //醒来后开始执行下面代码
    if (!busy_wait(hd)) { //若失败
      char error[64];
      sprintf(error, "%s read sector %d failed!!!!!!\n", hd->name, lba);
      PANIC(error);
    }

    //5把数据从硬盘的缓冲区中读出
    read_from_sector(hd, (void *)((uint32_t)buf + secs_done * 512), secs_op);
    secs_done += secs_op;
  }
  lock_release(&hd->my_channel->lock);
}

//将buf中sec_cnt扇区数据写入硬盘
//接受4个参数，硬盘hd、写入硬盘的扇区地址lba、待写入硬盘的数据所在的地址buf、
//待写入的数据以扇区大小为单位的数量sec_cnt。功能是将buf中sec_cnt扇区数据写入硬盘hd的lba扇区。
void ide_write(struct disk* hd, uint32_t lba, void* buf, uint32_t sec_cnt) {
  ASSERT(lba <= max_lba);
  ASSERT(sec_cnt > 0);
  lock_acquire (&hd->my_channel->lock);

  //1先选择操作的硬盘
  select_disk(hd);

  uint32_t secs_op;		      //每次操作的扇区数
  uint32_t secs_done = 0;	  //已完成的扇区数
  while(secs_done < sec_cnt) {
    if ((secs_done + 256) <= sec_cnt) {
      secs_op = 256;
    } else {
      secs_op = sec_cnt - secs_done;
    }

    //2写入待写入的扇区数和起始扇区号
    select_sector(hd, lba + secs_done, secs_op);

    //3执行的命令写入reg_cmd寄存器
    cmd_out(hd->my_channel, CMD_WRITE_SECTOR);  //准备开始写数据

    //4检测硬盘状态是否可读
    if (!busy_wait(hd)) {   //若失败
      char error[64];
      sprintf(error, "%s write sector %d failed!!!!!!\n", hd->name, lba);
      PANIC(error);
    }

    //5将数据写入硬盘
    write2sector(hd, (void*)((uint32_t)buf + secs_done * 512), secs_op);

    //在硬盘响应期间阻塞自己
    //对于写硬盘来说，驱动程序阻塞自己是在硬盘开始写扇区之后
    sema_down(&hd->my_channel->disk_done);
    secs_done += secs_op;
  }
  //醒来后开始释放锁
  lock_release(&hd->my_channel->lock);
}

//将dst中len个相邻字节交换位置后存入buf
//接受3个参数，目标数据地址dst、缓冲区buf、数据长度len，功能是将dst中
//len个相邻字节交换位置后存入buf，buf是dst最终转换的结果
//此函数用来处理identify命令的返回信息，硬盘参数信息是以字为单位的，包括偏移、长度的单位都是字，
//在这16位的字中，相邻字符的位置是互换的，所以通过此函数做转换。
static void swap_pairs_bytes(const char *dst, char *buf, uint32_t len) {
  uint8_t idx;
  for (idx = 0; idx < len; idx += 2) {
    //buf中存储dst中两相邻元素交换位置后的字符串
    buf[idx + 1] = *dst++;
    buf[idx] = *dst++;
  }
  buf[idx] = '\0';
}

//获得硬盘参数信息
//接受1个参数，硬盘hd。功能是向硬盘发送identify命令以获得硬盘参数信息
static void identify_disk(struct disk *hd) {
  char id_info[512];
  select_disk(hd);
  cmd_out(hd->my_channel, CMD_IDENTIFY);
  //向硬盘发送指令后便通过信号量阻塞自己，
  //待硬盘处理完成后，通过中断处理程序将自己唤醒
  sema_down(&hd->my_channel->disk_done);

  //醒来后开始执行下面代码
  if (!busy_wait(hd)) { //若失败
    char error[64];
    sprintf(error, "%s identify sector failed!!!!!!\n", hd->name);
    PANIC(error);
  }
  read_from_sector(hd, id_info, 1); //从硬盘获取信息到id_info

  char buf[64];   //是给swap_pairs_bytes使用的，用于存储转换的结果

  //identify返回的信息中，结果都是以字为单位，字偏移量10～19是硬盘序列号，长度为20的字符串
  //字偏移量27～46是硬盘型号，长度为40的字符串
  //字偏移量60～61是可供用户使用的扇区数，长度为2的整型
  //sn_start表示序列号起始字节地址
  //md_start表示型号起始字节地址
  uint8_t sn_start = 10 * 2, sn_len = 20, md_start = 27 * 2, md_len = 40;
  swap_pairs_bytes(&id_info[sn_start], buf, sn_len);
  printk("disk %s info:\nSN: %s\n", hd->name, buf);
  memset(buf, 0, sizeof(buf));
  swap_pairs_bytes(&id_info[md_start], buf, md_len);
  printk("MOUDLE: %s\n", buf);
  uint32_t sectors = *(uint32_t *)&id_info[60 * 2];
  printk("SECTORS: %d\n", sectors);
  printk("CAPACITY: %dMB\n", sectors * 512 / 1024 / 1024);
}

//扫描硬盘hd中地址为ext_lba的扇区中的所有分区
//接受2个参数，硬盘hd和扩展扇区地址ext_lba。功能是扫描硬盘hd中地址为ext_lba的扇区中的所有分区。
static void partition_scan(struct disk *hd, uint32_t ext_lba) {
  struct boot_sector *bs = sys_malloc(sizeof(struct boot_sector)); //由于递归，使用malloc
  ide_read(hd, ext_lba, bs, 1);
  uint8_t part_idx = 0;
  struct partition_table_entry *p = bs->partition_table;

  //硬盘可以分为4个分区，4个分区可以划分为3个主分区+1个总拓展分区，总拓展分区可以划分为多个子拓展分区
  //其中总拓展分区的ebr中分区表的第一项是第一个子拓展分区的逻辑分区（分区类型为0x66），
  //第二项是下一个子拓展分区的信息（下一个子拓展分区的ebr），分区类型为0x05
  //读取了下一个子拓展分区的ebr，其中分区表第一项是该子拓展分区的逻辑分区（分区类型为0x66），
  //第二项是下一个子拓展分区的信息（下一个子拓展分区的ebr），分区类型为0x05

  //遍历分区表4个分区表项
  while (part_idx++ < 4) {
    if (p->fs_type == 0x5) {    //若为扩展分区
      if (ext_lba_base != 0) {
        //子扩展分区的start_lba是相对于主引导扇区中的总扩展分区地址ext_lba_base
        partition_scan(hd, p->start_lba + ext_lba_base);
      } else {
        //ext_lba_base为0，这说明这是第一次调用partition_scan，此时获取的是MBR引导扇区中的分区表，
        //需要记录下总扩展分区的起始lba地址，因为后面所有的子扩展分区地址都相对于此
        //ext_lba_base为0表示是第一次读取引导块，也就是主引导记录所在的扇区
        //记录下扩展分区的起始lba地址，后面所有的扩展分区地址都相对于此
        ext_lba_base = p->start_lba;  //ext_lba_base是总扩展分区地址
        partition_scan(hd, p->start_lba);
      }
    } else if (p->fs_type != 0) {   //若是有效的分区类型，若为0，则表示empty，即无效的分区类型
      if (ext_lba == 0) {           //此时全是主分区
        hd->prim_parts[p_no].start_lba = ext_lba + p->start_lba;
        hd->prim_parts[p_no].sec_cnt = p->sec_cnt;
        hd->prim_parts[p_no].my_disk = hd;
        list_append(&partition_list, &hd->prim_parts[p_no].part_tag);
        sprintf(hd->prim_parts[p_no].name, "%s%d", hd->name, p_no + 1);
        p_no++;
        ASSERT(p_no < 4);     //0, 1, 2, 3
      } else {    //子拓展分区的逻辑分区
        hd->logic_parts[l_no].start_lba = ext_lba + p->start_lba;
        hd->logic_parts[l_no].sec_cnt = p->sec_cnt;
        hd->logic_parts[l_no].my_disk = hd;
        list_append(&partition_list, &hd->logic_parts[l_no].part_tag);
        sprintf(hd->logic_parts[l_no].name, "%s%d", hd->name, l_no + 5);  //逻辑分区数字从5开始，主分区是1～4
        l_no++;
        if (l_no >= 8)    // 只支持8个逻辑分区，避免数组越界
          return;
      }
    }
    p++;
  }
  sys_free(bs);
}

//打印分区信息
static bool partition_info(struct list_elem* pelem, int arg UNUSED) {
  struct partition* part = elem2entry(struct partition, part_tag, pelem);
  printk("%s start_lba:0x%x, sec_cnt:0x%x\n", part->name, part->start_lba, part->sec_cnt);

  //在此处return false与函数本身功能无关,只是为了让主调函数list_traversal继续向下遍历元素
  return false;
}

void intr_hd_handler(uint8_t irq_no) {
  ASSERT(irq_no == 0x2e || irq_no == 0x2f);
  uint8_t ch_no = irq_no - 0x2e;  //中断通道在通道数组channels中的索引值
  struct ide_channel *channel = &channels[ch_no];
  ASSERT(channel->irq_no == irq_no);
  //不必担心此中断是否对应的是这一次的expecting_intr，
  //每次读写硬盘时会申请锁，从而保证了同步一致性
  if (channel->expecting_intr) {
    channel->expecting_intr = false;
    sema_up(&channel->disk_done);
    //取状态寄存器使硬盘控制器认为此次的中断已被处理，从而硬盘可以继续执行新的读写
    //中断处理完成后，需要显式通知硬盘控制器此次中断已经处理完成，否则硬盘便不会产生新的中断，
    //这也是为了保证数据的有效性和安全性。硬盘控制器的中断在下列情况下会被清掉。
    //读取了status寄存器。
    //发出了reset命令。
    //或者又向reg_cmd写了新的命令。
    inb(reg_status(channel));
  }
}

//硬盘数据结构初始化
void ide_init() {
  printk("ide_init start\n");

  uint8_t hd_cnt = *((uint8_t *)(0x475)); //在地址0x475处获取硬盘的数量，该地址中的硬盘数量由bios负责写入
  printk("ide_init hd_cnt:%d\n", hd_cnt);
  ASSERT(hd_cnt > 0);
  list_init(&partition_list);
  //一个ide通道上有两个硬盘，根据硬盘数量反推有几个ide通道
  channel_cnt = DIV_ROUND_UP(hd_cnt, 2);
  struct ide_channel *channel;
  uint8_t channel_no = 0, dev_no = 0;

  //处理每个通道上的硬盘
  while (channel_no < channel_cnt) {
    channel = &channels[channel_no];
    sprintf(channel->name, "ide%d", channel_no);

    //为每个ide通道初始化端口基址及中断向量
    switch (channel_no) {
      case 0:
        channel->port_base = 0x1f0; //ide0通道的起始端口号是0x1f0
        channel->irq_no = 0x20 + 14;  //从片8259a上倒数第二的中断引脚（0x2e）。硬盘，也就是ide0通道的中断向量号
        break;
      case 1:
        channel->port_base = 0x170; //ide1通道的起始端口号是0x170
        channel->irq_no = 0x20 + 15;  //从片8259A上的最后一个中断引脚（0x2f）。我们用来响应ide1通道上的硬盘中断
        break;
    }
    //未向硬盘写入指令时不期待硬盘的中断
    channel->expecting_intr = false;
    lock_init(&channel->lock);
    //初始化为0，目的是向硬盘控制器请求数据后，硬盘驱动sema_down此信号量会阻塞线程，
    //直到硬盘完成后通过发中断，由中断处理程序将此信号量sema_up，唤醒线程
    sema_init(&channel->disk_done, 0);

    register_handler(channel->irq_no, intr_hd_handler);

    //分别获取两个硬盘的参数及分区信息
    while (dev_no < 2) {
      struct disk *hd = &channel->devices[dev_no];
      hd->my_channel = channel;
      hd->dev_no = dev_no;
      sprintf(hd->name, "sd%c", 'a' + channel_no * 2 + dev_no);
      identify_disk(hd);    //获取硬盘参数
      if (dev_no != 0) {    //内核本身的裸硬盘（os.img）不处理
        partition_scan(hd, 0);    //扫描该硬盘上的分区
      }
      p_no = 0, l_no = 0;
      dev_no++;
    }

    dev_no = 0;  // 将硬盘驱动器号置0,为下一个channel的两个硬盘初始化。
    channel_no++;   //下一个channel
  }

  printk("\nall partition info\n");
  //打印所有分区信息
  list_traversal(&partition_list, partition_info, (int)NULL);
  printk("ide_init done\n");
}