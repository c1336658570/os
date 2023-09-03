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
  uint32_t fd_idx = 3;  //前3个成员预留给标准输入、标准输出及标准错误
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

/*
创建文件需要考虑哪些工作:
（1）文件需要inode来描述大小、位置等属性，所以创建文件就要创建其inode。
这就涉及到向inode_bitmap申请位图来获得inode号，因此inode_bitmap会被更新，
inode_table数组中的某项也会由新的inode填充。
（2）inode->i_sectors是文件具体存储的扇区地址，这需要向block_bitmap申请可用位来获得可用的块
（在我们这里，为简化处理，1块等于1扇区），因此block_bitmap会被更新，
分区的数据区data_start_lba以后的某个扇区会被分配。
（3）新增加的文件必然存在于某个目录，所以该目录的inode->i_size会增加个目录项的大小。
此新增加的文件对应的目录项需要写入该目录的inode->i_sectors[]中的某个扇区，
原有扇区可能已满，所以有可能要申请新扇区来存储目录项。
（4）若其中某步操作失败，需要回滚之前已成功的操作。
（5）inode_bitmap、block_bitmap、新文件的inode及文件所在目录的inode，这些位于内存中已经被改变的数据要同步到硬盘。
*/
//创建文件，若成功则返回文件描述符，否则返回-1
//接受3个参数，父目录partent_dir、文件名filename、创建标识flag，
//功能是在目录parent_dir中以模式flag去创建普通文件filename，
//若成功则返回文件描述符，即pcb->fd_table中的下标，否则返回−1。
int32_t file_create(struct dir *parent_dir, char *filename, uint8_t flag) {
  //创建新文件的顺序是：创建文件i结点->文件描述符fd->目录项。
  //后续操作的公共缓冲区
  void *io_buf = sys_malloc(1024);
  if (io_buf == NULL) {
    printk("in file_creat: sys_malloc for io_buf failed\n");
    return -1;
  }

  uint8_t rollback_step = 0;    //用于操作失败时回滚各资源状态

  //为新文件分配inode
  int32_t inode_no = inode_bitmap_alloc(cur_part); //为新文件分配inode
  if (inode_no == -1) {
    printk("in file_creat: allocate inode failed\n");
    return -1;
  }

  //此inode要从堆中申请内存，不可生成局部变量（函数退出时会释放，
  //因为file_table数组中的文件描述符的inode指针要指向它
  struct task_struct *cur = running_thread();
  uint32_t *cur_pagedir_bak = cur->pgdir;
  cur->pgdir = NULL;
  //以上三行代码完成后下面分配的内存将位于内核区
  struct inode *new_file_inode = (struct inode *)sys_malloc(sizeof(struct inode));
  if (new_file_inode == NULL) {
    printk("file_create: sys_malloc for inode failded\n");
    rollback_step = 1;
    goto rollback;
  }
  //恢复pgdir
  cur->pgdir = cur_pagedir_bak;
  
  inode_init(inode_no, new_file_inode);

  //返回的是file_table数组的下标
  int fd_idx = get_free_slot_in_global();   //获取空闲文件结构的下标
  if (fd_idx == -1) {
    printk("exceed max open files\n");
    rollback_step = 2;
    goto rollback;
  }

  //初始化文件表中的文件结构
  file_table[fd_idx].fd_inode = new_file_inode;
  file_table[fd_idx].fd_flag = flag;
  file_table[fd_idx].fd_pos = 0;
  file_table[fd_idx].fd_inode->write_deny = false;

  struct dir_entry new_dir_entry;
  memset(&new_dir_entry, 0, sizeof(struct dir_entry));

  //create_dir_entry只是内存操作不出意外，不会返回失败
  //用filename、inode_no和FT_REGULAR填充new_dir_entry
  create_dir_entry(filename, inode_no, FT_REGULAR, &new_dir_entry);

  //同步内存数据到硬盘
  //a   在目录parent_dir下安装目录项new_dir_entry，写入硬盘后返回true，否则false
  if(!sync_dir_entry(parent_dir, &new_dir_entry, io_buf)) { //将new_dir_entry写入到父目录parent_dir中
    printk("sync dir_entry to disk failed\n");
    rollback_step = 3;
    goto rollback;
  }

  memset(io_buf, 0, 1024);
  //b   将父目录i结点的内容同步到硬盘   sync_dir_entry会改变父目录inode中的信息
  inode_sync(cur_part, parent_dir->inode, io_buf);

  memset(io_buf, 0, 1024);
  //c   将新创建文件的i结点内容同步到硬盘
  inode_sync(cur_part, new_file_inode, io_buf);

  //d   将inode_bitmap位图同步到硬盘
  bitmap_sync(cur_part, inode_no, INODE_BITMAP);

  //e   将创建的文件i结点添加到open_inodes链表
  list_push(&cur_part->open_inodes, &new_file_inode->inode_tag);
  new_file_inode->i_open_cnts = 1;

  sys_free(io_buf);
  return pcb_fd_install(fd_idx);

  //创建文件需要创建相关的多个资源，若某步失败则会执行到下面的回滚步骤
rollback:
  switch (rollback_step) {
    case 3:
      //失败时，将file_table中的相应位清空
      memset(&file_table[fd_idx], 0, sizeof(struct file));
    case 2:
      cur->pgdir = NULL;
      sys_free(new_file_inode);
      //恢复pgdir
      cur->pgdir = cur_pagedir_bak;
    case 1:
      //如果新文件的i结点创建失败，之前位图中分配的inode_no也要恢复
      bitmap_set(&cur_part->inode_bitmap, inode_no, 0);
      break;
  }
  sys_free(io_buf);
  return -1;
}

//打开编号为inode_no的inode对应的文件，若成功则返回文件描述符，否则返回-1
//接受2个参数，inode编号inode_no和打开标识flag，函数功能是打开编号为inode_no的inode对应的文件，
//若成功则返回文件描述符，否则返回−1。
int32_t file_open(uint32_t inode_no, uint8_t flag) {
  int fd_idx = get_free_slot_in_global();   //从文件表file_table中获取空位的下标
  if (fd_idx == -1) {
    printk("exceed max open files\n");
    return -1;
  }
  file_table[fd_idx].fd_inode = inode_open(cur_part, inode_no);
  file_table[fd_idx].fd_pos = 0;
  //每次打开文件，要将fd_pos还原为0，即让文件内的指针指向开头
  file_table[fd_idx].fd_flag = flag;
  bool *write_deny = &file_table[fd_idx].fd_inode->write_deny;

  if (flag & O_WRONLY || flag & O_RDWR) {
    //只要是关于写文件，判断是否有其他进程正写此文件
    //若是读文件，不考虑 write_deny
    //以下进入临界区前先关中断
    enum intr_status old_status = intr_disable();
    if (!(*write_deny)) {   //若当前没有其他进程写该文件，将其占用
      *write_deny = true;   //true，避免多个进程同时写此文件
      intr_set_status(old_status);    //恢复中断
    } else {
      intr_set_status(old_status);
      printk("file can’t be write now, try again later\n");
      return -1;
    }
  }
  //若是读文件或创建文件(O_RDONLY或O_CREAT)，不用理会write_deny，保持默认
  return pcb_fd_install(fd_idx);
}

//关闭文件
//接受1个参数，文件file，功能是关闭文件，成功则返回0，否则返回−1。
int32_t file_close(struct file *file) {
  if (file == NULL) {
    return -1;
  }
  file->fd_inode->write_deny = false;
  inode_close(file->fd_inode);
  file->fd_inode = NULL;      //使文件结构可用
  return 0;
}

//把buf中的count个字节写入file，成功则返回写入的字节数，失败则返回-1
//接受3个参数，文件file、数据缓冲区buf、字节数count，功能是把buf中的count个字节写入file，
//成功则返回写入的字节数，失败则返回−1。
int32_t file_write(struct file *file, const void *buf, uint32_t count) {
  if ((file->fd_inode->i_size + count) > (BLOCK_SIZE * 140)) {  //文件最大尺寸是140个块
    //文件目前最大只支持 512*140=71680 字节
    printk("exceed max file_size 71680 bytes, write file failed\n");
    return -1;
  }
  //磁盘操作都以1个扇区为单位，所以申请了512字节的缓冲区给io_buf
  uint8_t *io_buf = sys_malloc(512);
  if (io_buf == NULL) {
    printk("file_write: sys_malloc for io_buf failed\n");
    return -1;
  }
  //用来记录文件所有的块地址，128个间接块+12个直接块的大小
  uint32_t *all_blocks = (uint32_t *)sys_malloc(BLOCK_SIZE + 48);
  if (all_blocks == NULL) {
    printk("file_write: sys_malloc for all_blocks failed\n");
    return -1;
  }

  const uint8_t* src = buf;	      //用src指向buf中待写入的数据 
  uint32_t bytes_written = 0;	    //用来记录已写入数据大小
  uint32_t size_left = count;	    //用来记录未写入数据大小
  int32_t block_lba = -1;	        //块地址
  uint32_t block_bitmap_idx = 0;  //用来记录block对应于block_bitmap中的索引,做为参数传给bitmap_sync
  uint32_t sec_idx;	              //用来索引扇区
  uint32_t sec_lba;	              //扇区地址
  uint32_t sec_off_bytes;         //扇区内字节偏移量
  uint32_t sec_left_bytes;        //扇区内剩余字节量
  uint32_t chunk_size;	          //每次写入硬盘的数据块大小
  int32_t indirect_block_table;   //用来获取一级间接表地址
  uint32_t block_idx;		          //块索引

  //判断文件是否是第一次写,如果是,先为其分配一个块
  if (file->fd_inode->i_sectors[0] == 0) {
    block_lba = block_bitmap_alloc(cur_part);
    if (block_lba == -1) {
      printk("file_write: block_bitmap_alloc failed\n");
      return -1;
    }
    file->fd_inode->i_sectors[0] = block_lba;

    //每分配一个块就将位图同步到硬盘
    block_bitmap_idx = block_lba - cur_part->sb->data_start_lba;
    ASSERT(block_bitmap_idx != 0);
    bitmap_sync(cur_part, block_bitmap_idx, BLOCK_BITMAP);
  }

  //下面判断文件是否要为这count个字节分配新块，也就是现有的扇区是否够用

  //写入count个字节前,该文件已经占用的块数
  uint32_t file_has_used_blocks = file->fd_inode->i_size / BLOCK_SIZE + 1;

  //存储count字节后该文件将占用的块数
  uint32_t file_will_use_blocks = (file->fd_inode->i_size + count) / BLOCK_SIZE + 1;
  ASSERT(file_will_use_blocks <= 140);

  //通过此增量判断是否需要分配扇区,如增量为0,表示原扇区够用
  uint32_t add_blocks = file_will_use_blocks - file_has_used_blocks;

  //318-434行把文件现在使用及未来使用的块地址收集到all_blocks中（不包括那些不参与写入操作的块地址），与数据写入无关
  //收集工作完成之后，all_block 中包括原块地址及新数据占用的块地址，在这之后我们才进行数据写入工作。

  //开始将文件所有块地址收集到all_blocks,(系统中块大小等于扇区大小)
  //后面都统一在all_blocks中获取写入扇区地址
  if (add_blocks == 0) {
    //如果add_blocks为0，这说明count值小于等于原有扇区中的剩余空间，剩余空间便可容纳count个字节数据，无需再申请新的块
    //在同一扇区内写入数据,不涉及到分配新扇区
    //判断原有块地址是否为直接块
    if (file_has_used_blocks <= 12 ) {	      //文件数据量将在12块之内
      block_idx = file_has_used_blocks - 1;   //指向最后一个已有数据的扇区
      all_blocks[block_idx] = file->fd_inode->i_sectors[block_idx];
    } else { 
      //未写入新数据之前已经占用了间接块,需要将间接块地址读进来
      ASSERT(file->fd_inode->i_sectors[12] != 0);
      indirect_block_table = file->fd_inode->i_sectors[12];
      ide_read(cur_part->my_disk, indirect_block_table, all_blocks + 12, 1);
    }
  } else {
    //若有增量,便涉及到分配新扇区及是否分配一级间接块表,下面要分三种情况处理
    //第一种情况:12个直接块够用
    //若已经使用的扇区数在12块之内，新增了若干块后，文件大小还在12块之内，直接分配所需的
    //块并把块地址写入i_sectors数组中即可。
    if (file_will_use_blocks <= 12 ) {
      //先将有剩余空间的可继续用的扇区地址写入all_blocks
      block_idx = file_has_used_blocks - 1;
      ASSERT(file->fd_inode->i_sectors[block_idx] != 0);
      all_blocks[block_idx] = file->fd_inode->i_sectors[block_idx];

      //再将未来要用的扇区分配好后写入all_blocks
      block_idx = file_has_used_blocks;       //指向第一个要分配的新扇区
      while (block_idx < file_will_use_blocks) {
        block_lba = block_bitmap_alloc(cur_part);
        if (block_lba == -1) {
          printk("file_write: block_bitmap_alloc for situation 1 failed\n");
          return -1;
        }

        //写文件时,不应该存在块未使用但已经分配扇区的情况,当文件删除时,就会把块地址清0
        ASSERT(file->fd_inode->i_sectors[block_idx] == 0);    //确保尚未分配扇区地址
        file->fd_inode->i_sectors[block_idx] = all_blocks[block_idx] = block_lba;

        //每分配一个块就将位图同步到硬盘
        block_bitmap_idx = block_lba - cur_part->sb->data_start_lba;
        bitmap_sync(cur_part, block_bitmap_idx, BLOCK_BITMAP);

        block_idx++;      //下一个分配的新扇区
      }
    } else if (file_has_used_blocks <= 12 && file_will_use_blocks > 12) { 
      //第二种情况: 旧数据在12个直接块内,新数据将使用间接块
      //若已经使用的块数在12块之内，新增了若干块后，文件大小超过了12块，
      //这种情况下所申请的块除了要写入i_sector数组，还要创建一级间接块表并写入块地址。

      //先将有剩余空间的可继续用的扇区地址收集到all_blocks
      block_idx = file_has_used_blocks - 1;         //指向旧数据所在的最后一个扇区
      all_blocks[block_idx] = file->fd_inode->i_sectors[block_idx];

      //创建一级间接块表
      block_lba = block_bitmap_alloc(cur_part);
      if (block_lba == -1) {
        printk("file_write: block_bitmap_alloc for situation 2 failed\n");
        return -1;
      }

      ASSERT(file->fd_inode->i_sectors[12] == 0);     //确保一级间接块表未分配
      //分配一级间接块索引表
      indirect_block_table = file->fd_inode->i_sectors[12] = block_lba;

      block_idx = file_has_used_blocks;	    //第一个未使用的块,即本文件最后一个已经使用的直接块的下一块
      while (block_idx < file_will_use_blocks) {
        block_lba = block_bitmap_alloc(cur_part);
        if (block_lba == -1) {
          printk("file_write: block_bitmap_alloc for situation 2 failed\n");
          return -1;
        }

        if (block_idx < 12) {         //新创建的0~11块直接存入all_blocks数组
          ASSERT(file->fd_inode->i_sectors[block_idx] == 0);    //确保尚未分配扇区地址
          file->fd_inode->i_sectors[block_idx] = all_blocks[block_idx] = block_lba;
        } else {      //间接块只写入到all_block数组中,待全部分配完成后一次性同步到硬盘
          all_blocks[block_idx] = block_lba;
        }

        //每分配一个块就将位图同步到硬盘
        block_bitmap_idx = block_lba - cur_part->sb->data_start_lba;
        bitmap_sync(cur_part, block_bitmap_idx, BLOCK_BITMAP);

        block_idx++;      //下一个新扇区
      }
      ide_write(cur_part->my_disk, indirect_block_table, all_blocks + 12, 1); //同步一级间接块表到硬盘
    } else if (file_has_used_blocks > 12) {
      //第三种情况:新数据占据间接块
      //若已经使用的扇区数超过了12块，这种情况下要在一级间接块表中创建间接块项，
      //间接块项就是在一级间接块表中记录间接块地址的条目
      ASSERT(file->fd_inode->i_sectors[12] != 0);             //已经具备了一级间接块表
      indirect_block_table = file->fd_inode->i_sectors[12];	  //获取一级间接表地址

      //已使用的间接块也将被读入all_blocks,无须单独收录
      ide_read(cur_part->my_disk, indirect_block_table, all_blocks + 12, 1);  //获取所有间接块地址

      block_idx = file_has_used_blocks;   //第一个未使用的间接块,即已经使用的间接块的下一块
      while (block_idx < file_will_use_blocks) {
        block_lba = block_bitmap_alloc(cur_part);
        if (block_lba == -1) {
          printk("file_write: block_bitmap_alloc for situation 3 failed\n");
          return -1;
        }
        all_blocks[block_idx++] = block_lba;

        //每分配一个块就将位图同步到硬盘
        block_bitmap_idx = block_lba - cur_part->sb->data_start_lba;
        bitmap_sync(cur_part, block_bitmap_idx, BLOCK_BITMAP);
      }
      ide_write(cur_part->my_disk, indirect_block_table, all_blocks + 12, 1);   //同步一级间接块表到硬盘
    } 
  }

  //标识本次写入操作中第一个所写入的块，除第一次写入数据外，
  //通常情况下该块中都有些数据，最新的数据要从接着该块的空闲空间接着写
  bool first_write_block = true;          //含有剩余空间的扇区标识
  //块地址已经收集到all_blocks中,下面开始写数据
  file->fd_pos = file->fd_inode->i_size - 1;  //置fd_pos为文件大小-1,下面在写数据时随时更新
  while (bytes_written < count) {     //直到写完所有数据
    memset(io_buf, 0, BLOCK_SIZE);
    sec_idx = file->fd_inode->i_size / BLOCK_SIZE;  //根据文件大小计算出的块索引，也就是扇区索引值
    sec_lba = all_blocks[sec_idx];    //获得扇区地址sec_lba
    sec_off_bytes = file->fd_inode->i_size % BLOCK_SIZE;  //数据在最后一个块中的偏移量
    sec_left_bytes = BLOCK_SIZE - sec_off_bytes;    //块中的可用字节空间

    //判断此次写入硬盘的数据大小
    //chunk_size是本次写入扇区的数据量
    //size_left表示剩下未写入硬盘的数据量，当size_left小于本次所写入块的剩余空间时，
    //chunk_size就等于size_left，这属于剩余的数据不足一个块，往最后一个块中写剩余数据时的情况，
    //如果size_left大于等于sec_left_bytes，那就把剩下的空闲空间写满，即chunk_size等于sec_left_bytes
    chunk_size = size_left < sec_left_bytes ? size_left : sec_left_bytes;
    if (first_write_block) {
      //判断，若马上要读写的块是本次操作中的第一个块，通常情况下该块中都已存在数据，因此
      //在下一行先将该块中的数据读出来，然后使first_write_block置为false。
      ide_read(cur_part->my_disk, sec_lba, io_buf, 1);
      first_write_block = false;
    }
    memcpy(io_buf + sec_off_bytes, src, chunk_size);
    ide_write(cur_part->my_disk, sec_lba, io_buf, 1);
    //printk("file write at lba 0x%x  ", sec_lba);    //调试,完成后去掉

    src += chunk_size;        //将指针推移到下个新数据
    file->fd_inode->i_size += chunk_size;   //更新文件大小
    file->fd_pos += chunk_size;   
    bytes_written += chunk_size;
    size_left -= chunk_size;
  }

  void *inode_buf = sys_malloc(BLOCK_SIZE * 2);
  if (inode_buf == NULL) {
    printk("file_write: sys_malloc for inode_buf failed\n");
    return -1;
  }
  inode_sync(cur_part, file->fd_inode, inode_buf);
  sys_free(inode_buf);

  //inode_sync(cur_part, file->fd_inode, io_buf);   //同步inode
  sys_free(all_blocks);
  sys_free(io_buf);
  return bytes_written;
}

//从文件file中读取count个字节写入buf, 返回读出的字节数,若到文件尾则返回-1
//接受3个参数，读取的文件file、数据写入的缓冲区buf、读取的字节数count，
//功能是从文件file中读取count个字节写入buf，返回读出的字节数，若到文件尾，则返回−1。
int32_t file_read(struct file* file, void* buf, uint32_t count) {
  uint8_t* buf_dst = (uint8_t*)buf;
  uint32_t size = count, size_left = size;

  //若要读取的字节数超过了文件可读的剩余量, 就用剩余量做为待读取的字节数
  if ((file->fd_pos + count) > file->fd_inode->i_size)	{
    size = file->fd_inode->i_size - file->fd_pos;
    size_left = size;
    if (size == 0) {	    //若到文件尾则返回-1
      return -1;
    }
  }

  uint8_t* io_buf = sys_malloc(BLOCK_SIZE);   //硬盘操作的缓冲区
  if (io_buf == NULL) {
    printk("file_read: sys_malloc for io_buf failed\n");
  }
  //all_blocks依然用来记录文件所有的块地址，我们后面的读硬盘操作将在此结构中获取地址。(12+128)
  uint32_t* all_blocks = (uint32_t*)sys_malloc(BLOCK_SIZE + 48);    //用来记录文件所有的块地址
  if (all_blocks == NULL) {
    printk("file_read: sys_malloc for all_blocks failed\n");
    return -1;
  }

  uint32_t block_read_start_idx = file->fd_pos / BLOCK_SIZE;		      //数据所在块的起始地址
  uint32_t block_read_end_idx = (file->fd_pos + size) / BLOCK_SIZE;   //数据所在块的终止地址
  uint32_t read_blocks = block_read_start_idx - block_read_end_idx;	  //如增量为0,表示数据在同一扇区
  ASSERT(block_read_start_idx < 139 && block_read_end_idx < 139);

  int32_t indirect_block_table;     //用来获取一级间接表地址
  uint32_t block_idx;		            //获取待读的块地址 

  //以下开始构建all_blocks块地址数组,专门存储用到的块地址(本程序中块大小同扇区大小)
  //如果read_blocks为0，说明要读取的count个字节在一个块中，因此只需要读取1个块
  if (read_blocks == 0) {     //在同一扇区内读数据,不涉及到跨扇区读取
    ASSERT(block_read_end_idx == block_read_start_idx);
    if (block_read_end_idx < 12 ) {	    //待读的数据在12个直接块之内
      block_idx = block_read_end_idx;
      all_blocks[block_idx] = file->fd_inode->i_sectors[block_idx];
    } else {    //若用到了一级间接块表,需要将表中间接块读进来
      indirect_block_table = file->fd_inode->i_sectors[12];
      ide_read(cur_part->my_disk, indirect_block_table, all_blocks + 12, 1);
    }
  } else {        //若要读多个块
    //第一种情况: 起始块和终止块属于直接块
    //若起始块和终止块都在12块之内，直接读入i_sectors数组中即可。
    if (block_read_end_idx < 12 ) {	      //数据结束所在的块属于直接块
      block_idx = block_read_start_idx; 
      while (block_idx <= block_read_end_idx) {
        all_blocks[block_idx] = file->fd_inode->i_sectors[block_idx]; 
        block_idx++;
      }
    } else if (block_read_start_idx < 12 && block_read_end_idx >= 12) {
      //第二种情况: 待读入的数据跨越直接块和间接块两类
      //先将直接块地址写入all_blocks
      //若起始块在12块之内，结束块超过了12块，除了要读入i_sector数组，还要从一级间接块索引表中读取间接块地址。
      block_idx = block_read_start_idx;
      while (block_idx < 12) {
        all_blocks[block_idx] = file->fd_inode->i_sectors[block_idx];
        block_idx++;
      }
      ASSERT(file->fd_inode->i_sectors[12] != 0);   //确保已经分配了一级间接块表

      //再将间接块地址写入all_blocks 
      indirect_block_table = file->fd_inode->i_sectors[12];
      ide_read(cur_part->my_disk, indirect_block_table, all_blocks + 12, 1);  //将一级间接块表读进来写入到第13个块的位置之后
    } else {	
      //第三种情况: 数据在间接块中
      //若起始块超过了12块，这种情况下要在一级间接块索引表中读取间接块。
      ASSERT(file->fd_inode->i_sectors[12] != 0);	            //确保已经分配了一级间接块表
      indirect_block_table = file->fd_inode->i_sectors[12];	  //获取一级间接表地址
      ide_read(cur_part->my_disk, indirect_block_table, all_blocks + 12, 1);//将一级间接块表读进来写入到第13个块的位置之后
    } 
  }

  //用到的块地址已经收集到all_blocks中,下面开始读数据
  uint32_t sec_idx, sec_lba, sec_off_bytes, sec_left_bytes, chunk_size;
  uint32_t bytes_read = 0;
  while (bytes_read < size) {     //直到读完为止
    sec_idx = file->fd_pos / BLOCK_SIZE;
    sec_lba = all_blocks[sec_idx];
    sec_off_bytes = file->fd_pos % BLOCK_SIZE;
    sec_left_bytes = BLOCK_SIZE - sec_off_bytes;
    chunk_size = size_left < sec_left_bytes ? size_left : sec_left_bytes;   //待读入的数据大小

    //清0操作不是必须的,读入的数据会把io_buf中旧数据覆盖，而且最终返回给用户的数据是在dst_buf中，
    //据读取的准确性是由chunk_size把控的，因此即使io_buf中有垃圾数据，只要chunk_size正确就不会多读入错误的数据。
    memset(io_buf, 0, BLOCK_SIZE);
    ide_read(cur_part->my_disk, sec_lba, io_buf, 1);    //读取1个扇区
    memcpy(buf_dst, io_buf + sec_off_bytes, chunk_size);  //往dst_buf中拷贝chunk_size个字节。

    buf_dst += chunk_size;
    file->fd_pos += chunk_size;
    bytes_read += chunk_size;
    size_left -= chunk_size;
  }
  sys_free(all_blocks);
  sys_free(io_buf);
  return bytes_read;
}