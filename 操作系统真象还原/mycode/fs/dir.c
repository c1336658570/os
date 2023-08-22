#include "dir.h"
#include "stdint.h"
#include "inode.h"
#include "file.h"
#include "fs.h"
#include "stdio-kernel.h"
#include "global.h"
#include "debug.h"
#include "memory.h"
#include "string.h"
#include "interrupt.h"
#include "super_block.h"

struct dir root_dir;             // 根目录

//打开根目录
//接受一个参数，分区part，功能是打开分区part的根目录。
void open_root_dir(struct partition *part) {
  root_dir.inode = inode_open(part, part->sb->root_inode_no);   //打开根目录的inode
  root_dir.dir_pos = 0;
}

//在分区part上打开i结点为inode_no的目录并返回目录指针
//接受两个参数，分区part和inode编号inode_no，功能是在分区part上打开i结点为inode_no的目录并返回目录指针
struct dir *dir_open(struct partition *part, uint32_t inode_no) {
  struct dir *pdir = (struct dir *)sys_malloc(sizeof(struct dir));
  pdir->inode = inode_open(part, inode_no);
  pdir->dir_pos = 0;
  return pdir;
}

//在part分区内的pdir目录内寻找名为name的文件或目录，找到后返回true并将其目录项存入dir_e，否则返回false
//接受4个参数，分区part、目录指针pdir、文件名name、目录项指针dir_e，
//函数功能是在part分区内的pdir目录内寻找名为name的文件或目录，
//找到后返回true并将其目录项存入dir_e，否则返回false
bool search_dir_entry(struct partition *part, struct dir *pdir, const char *name, struct dir_entry *dir_e) {
  uint32_t block_cnt = 140;     //12个直接块+128个一级间接块=140块

  //12个直接块大小+128个间接块,共560字节
  uint32_t *all_blocks = (uint32_t *)sys_malloc(48 + 512);
  if (all_blocks == NULL) {
    printk("search_dir_entry: sys_malloc for all_blocks failed");
    return false;
  }

  uint32_t block_idx = 0;
  while (block_idx < 12) {
    all_blocks[block_idx] = pdir->inode->i_sectors[block_idx];
    block_idx++;
  }
  block_idx = 0;

  if (pdir->inode->i_sectors[12] != 0) {      //若含有一级间接块表
    ide_read(part->my_disk, pdir->inode->i_sectors[12], all_blocks + 12, 1);
  }
  //至此，all_blocks存储的是该文件或目录的所有扇区地址

  /*
  在处理inode_table时，是将它连续写在多个扇区中，也就是除最后一个扇区外，其他几个扇区
  都写满了inode，从而导致了inode跨扇区的情况，以至于在获取inode的时候要做额外判断，比较麻烦。
  吸取经验教训，我们在往目录中写目录项的时候，写入的都是完整的目录项，避免了目录项跨扇区的情况*/
  //写目录项的时候已保证目录项不跨扇区，这样读目录项时容易处理，只申请容纳1个扇区的内存
  uint8_t *buf = (uint8_t *)sys_malloc(SECTOR_SIZE);
  struct dir_entry *p_de = (struct dir_entry *)buf;
  //p_de为指向目录项的指针，值为buf起始地址
  uint32_t dir_entry_size = part->sb->dir_entry_size;
  uint32_t dir_entry_cnt = SECTOR_SIZE / dir_entry_size;    //1扇区内可容纳的目录项个数

  //开始在所有块中查找目录项
  while (block_idx < block_cnt) {		  
    //块地址为0时表示该块中无数据,继续在其它块中找
    if (all_blocks[block_idx] == 0) {   //如果扇区地址为0，这说明未分配扇区，跳过
      block_idx++;
      continue;
    }
    ide_read(part->my_disk, all_blocks[block_idx], buf, 1);   //读入1扇区数据到buf

    uint32_t dir_entry_idx = 0;
    //遍历扇区中所有目录项
    while (dir_entry_idx < dir_entry_cnt) {
      //若找到了,就直接复制整个目录项
      if (!strcmp(p_de->filename, name)) {
        memcpy(dir_e, p_de, dir_entry_size);
        sys_free(buf);
        sys_free(all_blocks);
        return true;
      }
      dir_entry_idx++;
      p_de++;
    }
    block_idx++;          //更新为all_blocks中的下一个扇区，读取新的扇区，重复以上查找过程
    p_de = (struct dir_entry*)buf;  //此时p_de已经指向扇区内最后一个完整目录项了,需要恢复p_de指向为buf
    memset(buf, 0, SECTOR_SIZE);	  //将buf清0,下次再用
  }
  sys_free(buf);
  sys_free(all_blocks);
  return false;
}

//关闭目录
//接受1个参数，目录指针dir，功能是关闭目录dir
void dir_close(struct dir *dir) {
  //*************根目录不能关闭***************
  //1根目录自打开后就不应该关闭，否则还需要再次open_root_dir();
  //2root_dir所在的内存是低端1MB之内（定义的全局变量root_dir），并非在堆中，free会出问题
  if (dir == &root_dir) {
    //不做任何处理直接返回
    return;
  }
  inode_close(dir->inode);
  sys_free(dir);
}

//在内存中初始化目录项p_de
//接受4个参数，文件名filename、inode编号inode_no、文件类型file_type、目录项指针p_de。
//功能是在内存中创建目录项p_de。
void create_dir_entry(char *filename, uint32_t inode_no, uint8_t file_type, struct dir_entry *p_de) {
  ASSERT(strlen(filename) <= MAX_FILE_NAME_LEN);

  //初始化目录项
  memcpy(p_de->filename, filename, strlen(filename));
  p_de->i_no = inode_no;
  p_de->f_type = file_type;
}

//将目录项p_de写入父目录parent_dir中，io_buf由主调函数提供
//接受3个参数，父目录parent_dir、目录项p_de、缓冲区io_buf，
//功能是将目录项p_de写入父目录parent_dir中，其中io_buf由主调函数提供。
bool sync_dir_entry(struct dir *parent_dir, struct dir_entry *p_de, void * io_buf) {
  struct inode *dir_inode = parent_dir->inode;
  uint32_t dir_size = dir_inode->i_size;    //i_siz是目录中目录项的大小之和
  uint32_t dir_entry_size = cur_part->sb->dir_entry_size;   //一个目录项的大小

  ASSERT(dir_size % dir_entry_size == 0);   //dir_size应该是dir_entry_size的整数倍

  uint32_t dir_entrys_per_sec = (512 / dir_entry_size);   //每扇区最大的目录项数目

  int32_t block_lba = -1;

  //将该目录的所有扇区地址（12个直接块+128个间接块）存入all_blocks
  uint8_t block_idx = 0;
  uint32_t all_blocks[140] = {0};   //all_blocks保存目录所有的块

  //将12个直接块存入all_blocks
  while (block_idx < 12) {  //将目录的12个直接块地址收集到数组all_blocks
    all_blocks[block_idx] = dir_inode->i_sectors[block_idx];
    block_idx++;
  }
  
  //使目录项指针dir_e指向缓冲区io_buf
  struct dir_entry *dir_e = (struct dir_entry *)io_buf;   //dir_e用来在io_buf中遍历目录项

  int32_t block_bitmap_idx = -1;

  //开始遍历所有块以寻找目录项空位，若已有扇区中没有空闲位，在不超过文件大小的情况下申请新扇区来存储新目录项
  block_idx = 0;
  //由于删除文件时会造成目录中存在空洞，也就是文件系统内的碎片，所以在写入文件时，要逐个目录
  //项查找空位，避免一味在目录的末尾添加目录项，而前面所有文件已被删除时，却还占用多个扇区的情况
  while (block_idx < 140) {
    //文件（包括目录）最大支持12个直接块+128个间接块＝140个块
    block_bitmap_idx = -1;
    //先判断扇区是否分配
    if (all_blocks[block_idx] == 0) {   //在三种情况下分配块
      block_lba = block_bitmap_alloc(cur_part);   //分配一扇区
      if (block_lba == -1) {
        printk("alloc block bitmap for sync_dir_entry failed\n");
        return false;
      }
      //每分配一个块就同步一次block_bitmap
      block_bitmap_idx = block_lba - cur_part->sb->data_start_lba;
      ASSERT(block_bitmap_idx != -1);
      bitmap_sync(cur_part, block_bitmap_idx, BLOCK_BITMAP);  //将块位图同步到硬盘

      block_bitmap_idx = -1;
      if (block_idx < 12) {   //若是直接块
        dir_inode->i_sectors[block_idx] = all_blocks[block_idx] = block_lba;
      } else if(block_idx == 12) {
        //若是尚未分配一级间接块表（block_idx等于12表示第0个间接块地址为0）
        dir_inode->i_sectors[12] = block_lba;   //将一级间接块索引表的地址写入i_sectors[12]
        //将上面分配的块作为一级间接块表地址
        block_lba = -1;
        block_lba = block_bitmap_alloc(cur_part);   //分配一扇区作为第0个间接块
        if (block_lba == -1) {    //分配扇区失败
          //回滚操作
          block_bitmap_idx = dir_inode->i_sectors[12] - cur_part->sb->data_start_lba;
          bitmap_set(&cur_part->block_bitmap, block_bitmap_idx, 0);
          //个人觉得此处应该回写磁盘，因为修改了位图信息，应该将块位图同步到磁盘
          bitmap_sync(cur_part, block_bitmap_idx, BLOCK_BITMAP);    //将块位图同步到硬盘
          dir_inode->i_sectors[12] = 0;
          printk("alloc block bitmap for sync_dir_entry failed\n");
          return false;
        }
        //每分配一个块就同步一次block_bitmap
        block_bitmap_idx = block_lba - cur_part->sb->data_start_lba;
        ASSERT(block_bitmap_idx != -1);
        bitmap_sync(cur_part, block_bitmap_idx, BLOCK_BITMAP);    //将块位图同步到硬盘

        all_blocks[12] = block_lba;   //将新分配的扇区地址更新到all_blocks[12]，这是第0个间接块的地址
        //把新分配的第0个间接块地址写入一级间接块表
        ide_write(cur_part->my_disk, dir_inode->i_sectors[12], all_blocks + 12, 1);
      }else {     //若是间接块未分配    说明已经遍历到间接块了
        all_blocks[block_idx] = block_lba;  //将最初分配的扇区地址block_lba录入all_blocks
        //把新分配的第(block_idx-12)个间接块地址写入一级间接块表
        ide_write(cur_part->my_disk, dir_inode->i_sectors[12], all_blocks + 12, 1);
      }
      //再将新目录项p_de写入新分配的间接块
      memset(io_buf, 0, 512);
      memcpy(io_buf, p_de, dir_entry_size);
      ide_write(cur_part->my_disk, all_blocks[block_idx], io_buf, 1);   //将目录项写入扇区
      dir_inode->i_size += dir_entry_size;
      return true;
    }
    //若第block_idx块已存在，将其读进内存，然后在该块中查找空目录项
    ide_read(cur_part->my_disk, all_blocks[block_idx], io_buf, 1);
    //在扇区内查找空目录项
    uint8_t dir_entry_idx = 0;
    while (dir_entry_idx < dir_entrys_per_sec) {
      if ((dir_e + dir_entry_idx)->f_type == FT_UNKNOWN) {
        //FT_UNKNOWN为0，无论是初始化，或是删除文件后，都会将f_type置为FT_UNKNOWN
        memcpy((dir_e + dir_entry_idx), p_de, dir_entry_size);
        ide_write(cur_part->my_disk, all_blocks[block_idx], io_buf, 1);

        dir_inode->i_size += dir_entry_size;
        return true;
      }
      dir_entry_idx++;
    }
    block_idx++;
  }
  //所有的扇区都满了，直接输出“directory is full!”并以false返回。
  printk("directory is full!\n");
  return false;
}