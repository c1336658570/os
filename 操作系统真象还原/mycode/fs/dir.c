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

//把分区part目录pdir中编号为inode_no的目录项删除。删除目录项相关的工作：
//（1）在文件所在的目录中擦除该文件的目录项，使其为0。
//（2）根目录是必须存在的，它是文件读写的根基，不应该被清空，它至少要保留1个块。
//如果目录项独占1个块，并且该块不是根目录最后一个块的话，将其回收。
//（3）目录inode的i_size是目录项大小的总和，因此还要将i_size减去一个目录项的单位大小。
//（4）目录inode改变后，要同步到硬盘。
//接受4个参数，分区part、目录pdir、inode编号inode_no、缓冲区io_buf，
//功能是把分区part目录pdir中编号为inode_no的目录项删除。
bool delete_dir_entry(struct partition *part, struct dir *pdir, uint32_t inode_no, void *io_buf) {
  struct inode *dir_inode = pdir->inode;
  uint32_t block_idx = 0, all_blocks[140] = {0};
  //收集目录全部块地址
  while (block_idx < 12) {
    all_blocks[block_idx] = dir_inode->i_sectors[block_idx];
    block_idx++;
  }
  if (dir_inode->i_sectors[12]) {
    ide_read(part->my_disk, dir_inode->i_sectors[12], all_blocks + 12, 1);
  }

  //目录项在存储时保证不会跨扇区
  uint32_t dir_entry_size = part->sb->dir_entry_size;
  uint32_t dir_entrys_per_sec = (SECTOR_SIZE / dir_entry_size);   //每扇区最大的目录项数目
  struct dir_entry *dir_e = (struct dir_entry *)io_buf;
  struct dir_entry *dir_entry_found = NULL;
  uint8_t dir_entry_idx, dir_entry_cnt;
  //is_dir_first_block表示当前的块（待删除的目录项所在的块）是否是目录中最初的那个块，若在
  //目录中创建很多文件或子目录后，目录会扩展到多个块中。根目录最初的块是在格式化分区时创建的，目
  //录“.”和“..”都在这个块中，因此目录项的名称若为“.”，该块便是目录的最初的块。
  //当删除一个目录项时，若该目录项所在的块上没有其他目录项了，或者是除
  //了“.”和“..”之外没有其他目录项，我们就将该块回收了
  bool is_dir_first_block = false;      //目录的第1个块
  
  //遍历所有块，寻找目录项
  block_idx = 0;
  while (block_idx < 140) {
    is_dir_first_block = false;
    if (all_blocks[block_idx] == 0) {
      block_idx++;
      continue;
    }
    dir_entry_idx = dir_entry_cnt = 0;
    memset(io_buf, 0, SECTOR_SIZE);
    //读取扇区，获得目录项
    ide_read(part->my_disk, all_blocks[block_idx], io_buf, 1);

    //遍历所有的目录项，统计该扇区的目录项数量及是否有待删除的目录项
    while (dir_entry_idx < dir_entrys_per_sec) {
      if ((dir_e + dir_entry_idx)->f_type != FT_UNKNOWN) {  //只要不是FT_UNKNOWN就表示该目录项有意义
        if(!strcmp((dir_e + dir_entry_idx)->filename, ".")) {
          //若判断出目录项的filename 为”.”，就表示当前的块是目录最初的块，因此将变量is_dir_first_block置为true。
          is_dir_first_block = true;
        } else if (strcmp((dir_e + dir_entry_idx)->filename, ".") && 
        strcmp((dir_e + dir_entry_idx)->filename, "..")) {
          //否则统计目录中除“.”和“..”之外的所有目录项，目录项总数存储在变量dir_entry_cnt中。
          //统计目录项个数的目的是判断删除目录项后是否回收该块，理由是若dir_entry_cnt为1，就表示该目录项独占一个块
          dir_entry_cnt++;    //统计此扇区内的目录项个数，用来判断删除目录项后是否回收该扇区
          if ((dir_e + dir_entry_idx)->i_no == inode_no) {  //如果目录项与待删除的inod编号相同，这说明找到了
            //如果找到此i结点，就将其记录在dir_entry_found
            ASSERT(dir_entry_found == NULL);
            //确保目录中只有一个编号为inode_no的inode
            //找到一次后dir_entry_found就不再是NULL
            dir_entry_found = dir_e + dir_entry_idx;
            //找到后也继续遍历，统计总共的目录项数
          }
        }
      }
      dir_entry_idx++;
    }

    //若此扇区未找到该目录项，继续在下个扇区中找
    if (dir_entry_found == NULL) {
      block_idx++;
      continue;
    }
    //在此扇区中找到目录项后，清除该目录项并判断是否回收扇区，随后退出循环直接返回
    ASSERT(dir_entry_cnt >= 1);
    //除目录第1个扇区外，若该扇区上只有该目录项自己，则将整个扇区回收
    if (dir_entry_cnt == 1 && !is_dir_first_block) {
      //如果当前块中目录项个数dir_entry_cnt为1，并且当前块并不是根目录最初的那个块，那么
      //就不需要擦除目录项，把当前块直接回收

      //a 在块位图中回收该块
      uint32_t block_bitmap_idx = all_blocks[block_idx] - part->sb->data_start_lba;
      bitmap_set(&part->block_bitmap, block_bitmap_idx, 0);
      bitmap_sync(cur_part, block_bitmap_idx, BLOCK_BITMAP);

      //b 将块地址从数组i_sectors或索引表中去掉
      if (block_idx < 12) {   //若是直接块
        dir_inode->i_sectors[block_idx] = 0;
      } else {    //在一级间接索引表中擦除该间接块地址
        //先判断一级间接索引表中间接块的数量，如果仅有这1个间接块，连同间接索引表所在的块一同回收
        uint32_t indirect_blocks = 0;
        uint32_t indirect_block_idx = 12;
        while (indirect_block_idx < 140) {
          if (all_blocks[indirect_block_idx]!= 0) {
            indirect_blocks++;
          }
        }
        ASSERT(indirect_blocks >= 1);   //包括当前间接块

        if (indirect_blocks > 1) {
          //间接索引表中还包括其他间接块，仅在索引表中擦除当前这个间接块地址
          all_blocks[block_idx] = 0;
          ide_write(part->my_disk, dir_inode->i_sectors[12], all_blocks + 12, 1);
        } else {    //间接索引表中就当前这1个间接块
          //直接把间接索引表所在的块回收，然后擦除间接索引表块地址

          //回收间接索引表所在的块
          block_bitmap_idx = dir_inode->i_sectors[12] - part->sb->data_start_lba;
          bitmap_set(&part->block_bitmap, block_bitmap_idx, 0);
          bitmap_sync(cur_part, block_bitmap_idx, BLOCK_BITMAP);

          //将间接索引表地址清0
          dir_inode->i_sectors[12] = 0;
        }
      }
    } else {    //如果当前块中的目录项个数为多个或者当前块是目录的最初块，仅将该目录项清空
      //擦除目录项，然后同步到硬盘
      memset(dir_entry_found, 0, dir_entry_size);
      ide_write(part->my_disk, all_blocks[block_idx], io_buf, 1);
    }
    //更新i结点信息并同步到硬盘
    ASSERT(dir_inode->i_size >= dir_entry_size);
    dir_inode->i_size -= dir_entry_size;    //将目录的inode->i_size减少一个目录项大小
    memset(io_buf, 0, SECTOR_SIZE * 2);
    inode_sync(part, dir_inode, io_buf);    //同步inode到硬盘

    return true;
  }
  //所有块中未找到则返回false,若出现这种情况应该是serarch_file出错了
  return false;
}

//读取目录，成功返回1个目录项，失败返回NULL
//接受1个参数，目录指针dir，功能是读取目录dir，成功返回1个目录项，失败返回NULL。
struct dir_entry *dir_read(struct dir *dir) {
  struct dir_entry *dir_e = (struct dir_entry *)dir->dir_buf;
  struct inode *dir_inode = dir->inode;
  uint32_t all_blocks[140] = {0}, block_cnt = 12;
  uint32_t block_idx = 0, dir_entry_idx = 0;

  //将目录所有块地址收集到all_blocks中
  while (block_idx < 12) {
    all_blocks[block_idx] = dir_inode->i_sectors[block_idx];
    block_idx++;
  }
  if (dir_inode->i_sectors[12] != 0) {
    ide_read(cur_part->my_disk, dir_inode->i_sectors[12], all_blocks + 12, 1);
    block_cnt = 140;
  }
  block_idx = 0;    //使块索引block_idx恢复为0

  uint32_t cur_dir_entry_pos = 0;   //当前目录项的偏移，此项用来判断是否是之前已经返回过的目录项
  uint32_t dir_entry_size = cur_part->sb->dir_entry_size;
  uint32_t dir_entrys_per_sec = SECTOR_SIZE / dir_entry_size;   //1扇区内可容纳的目录项个数

  //因为此目录内可能删除了某些文件或子目录,所以要遍历所有块
  while (block_idx < block_cnt) {
    //dir_pos 是目录的“游标”，用于记录下一个读写对象的地址，dir_pos用于指向目录中某个目录项的地址。
    if (dir->dir_pos >= dir_inode->i_size) {    //已经遍历了所有的目录项，直接返回NULL
      return NULL;
    }
    if (all_blocks[block_idx] == 0) {  //如果此块地址为0,即空块,继续读出下一块
      block_idx++;
      continue;
    }
    memset(dir_e, 0, SECTOR_SIZE);
    ide_read(cur_part->my_disk, all_blocks[block_idx], dir_e, 1);
    dir_entry_idx = 0;
    //遍历扇区内所有目录项
    while (dir_entry_idx < dir_entrys_per_sec) {
      if ((dir_e + dir_entry_idx)->f_type) {    //如果f_type不等于0,即不等于FT_UNKNOWN
        //判断是不是最新的目录项,避免返回曾经已经返回过的目录项
        if (cur_dir_entry_pos < dir->dir_pos) {
          //若cur_dir_entry_pos小于dir->dir_pos，这说明都是之前返回过的目录项，
          //因此将cur_dir_entry_pos加上目录项大小，并使目录项索引dir_entry_idx加1后，
          //跳过当前目录项，直到cur_dir_entry_pos等于dir->dir_pos，这才找到了该返回的目录项
          cur_dir_entry_pos += dir_entry_size;
          dir_entry_idx++;
          continue;
        }
        ASSERT(cur_dir_entry_pos == dir->dir_pos);
        dir->dir_pos += dir_entry_size;   //更新为新位置,即下一个返回的目录项地址
        return dir_e + dir_entry_idx;
      }
      dir_entry_idx++;
    }
    block_idx++;
  }
  
  /*
  //在目录大小内遍历
  //因为此目录内可能删除了某些文件或子目录,所以要遍历所有块 
  while (dir->dir_pos < dir_inode->i_size) {  
    //dir_pos 是目录的“游标”，用于记录下一个读写对象的地址，dir_pos用于指向目录中某个目录项的地址。
    if (dir->dir_pos >= dir_inode->i_size) {  //已经遍历了所有的目录项，直接返回NULL
      return NULL;
    }
    if (all_blocks[block_idx] == 0) {
      //如果此块地址为0，即空块，继续读出下一块
      block_idx++;
      continue;
    }
    memset(dir_e, 0, SECTOR_SIZE);
    ide_read(cur_part->my_disk, all_blocks[block_idx], dir_e, 1);
    dir_entry_idx = 0;
    //遍历扇区内所有目录项
    while (dir_entry_idx < dir_entrys_per_sec) {
      if ((dir_e + dir_entry_idx)->f_type) {    //如果f_type不等于0，即不等于FT_UNKNOWN
        //判断是不是最新的目录项，避免返回曾经已经返回过的目录项
        if (cur_dir_entry_pos < dir->dir_pos) { //cur_dir_entry_pos来表示当前目录项的地址
          //若cur_dir_entry_pos小于dir->dir_pos，这说明都是之前返回过的目录项，
          //因此将cur_dir_entry_pos加上目录项大小，并使目录项索引dir_entry_idx加1后，
          //跳过当前目录项，直到cur_dir_entry_pos等于dir->dir_pos，这才找到了该返回的目录项
          cur_dir_entry_pos += dir_entry_size;
          dir_entry_idx++;
          continue;
        }
        ASSERT(cur_dir_entry_pos == dir->dir_pos);
        dir->dir_pos += dir_entry_size;     //更新为新位置，即下一个返回的目录项地址
        return dir_e + dir_entry_idx;
      }
      dir_entry_idx++;
    }
    block_idx++;
  }
  */
  return NULL;
}

//判断目录是否为空
//dir_is_empty接受1个参数，目录指针dir，功能是判断目录dir是否为空
bool dir_is_empty(struct dir *dir) {
  struct inode *dir_inode = dir->inode;
  //若目录下只有.和..这两个目录项，则目录为空
  return (dir_inode->i_size == cur_part->sb->dir_entry_size * 2);
}

//在父目录parent_dir中删除child_dir
//接受2个参数，父目录指针parent_dir和子目录指针child_dir，
//功能是在父目录parent_dir中删除child_dir。成功删除则返回0，否则返回−1。
int32_t dir_remove(struct dir *parent_dir, struct dir *child_dir) {
  struct inode *child_dir_inode = child_dir->inode;
  //空目录只在inode->i_sectors[0]中有扇区，其他扇区都应该为空
  int32_t block_idx = 1;
  while (block_idx < 13) {
    ASSERT(child_dir_inode->i_sectors[block_idx] == 0);
    block_idx++;
  }
  void *io_buf = sys_malloc(SECTOR_SIZE * 2);
  if (io_buf == NULL) {
    printk("dir_remove: malloc for io_buf failed\n");
    return -1;
  }
  //在父目录parent_dir中删除子目录child_dir对应的目录项
  delete_dir_entry(cur_part, parent_dir, child_dir_inode->i_no, io_buf);

  //回收inode中i_secotrs中所占用的扇区，并同步inode_bitmap和block_bitmap
  inode_release(cur_part, child_dir_inode->i_no);   //释放子目录的inode
  sys_free(io_buf);
  return 0;
}