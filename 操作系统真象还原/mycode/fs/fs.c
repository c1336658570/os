#include "fs.h"
#include "super_block.h"
#include "inode.h"
#include "dir.h"
#include "stdint.h"
#include "stdio-kernel.h"
#include "list.h"
#include "string.h"
#include "ide.h"
#include "global.h"
#include "debug.h"
#include "memory.h"
#include "file.h"
#include "console.h"
#include "keyboard.h"
#include "ioqueue.h"
#include "pipe.h"

struct partition *cur_part;     //默认情况下操作的是哪个分区

//在分区链表中找到名为part_name的分区，并将其指针赋值给cur_part
static bool mount_partition(struct list_elem *pelem, int arg) {
  char *part_name = (char *)arg;
  struct partition *part = elem2entry(struct partition, part_tag, pelem);
  if (!strcmp(part->name, part_name)) {   //如果相等则找到了该分区
    cur_part = part;
    struct disk *hd = cur_part->my_disk;    //获得分区所在的硬盘

    //sb_buf用来存储从硬盘上读入的超级块
    struct super_block *sb_buf = (struct super_block *)sys_malloc(SECTOR_SIZE);
    
    //在内存中创建分区cur_part的超级块
    cur_part->sb = (struct super_block *)sys_malloc(sizeof(struct super_block));
    if (cur_part->sb == NULL) {
      PANIC("alloc memory failed!");
    }

    //读入超级块
    memset(sb_buf, 0, SECTOR_SIZE);
    ide_read(hd, cur_part->start_lba + 1, sb_buf, 1);

    //把sb_buf中超级块的信息复制到分区的超级块sb中
    memcpy(cur_part->sb, sb_buf, sizeof(struct super_block));

    //**********将硬盘上的块位图读入到内存**************
    cur_part->block_bitmap.bits = (uint8_t *)sys_malloc(sb_buf->block_bitmap_sects * SECTOR_SIZE);
    if (cur_part->block_bitmap.bits == NULL) {
      PANIC("alloc memory failed!");
    }
    cur_part->block_bitmap.btmp_bytes_len = sb_buf->block_bitmap_sects * SECTOR_SIZE;

    //从硬盘上读入块位图到分区的block_bitmap.bits
    ide_read(hd, sb_buf->block_bitmap_lba, cur_part->block_bitmap.bits, sb_buf->block_bitmap_sects);

    //**********将硬盘上的inode位图读入到内存************
    cur_part->inode_bitmap.bits = (uint8_t *)sys_malloc(sb_buf->inode_bitmap_sects * SECTOR_SIZE);
    if (cur_part->inode_bitmap.bits == NULL) {
      PANIC("alloc memory failed!");
    }
    cur_part->inode_bitmap.btmp_bytes_len = sb_buf->inode_bitmap_sects * SECTOR_SIZE;
    //从硬盘上读入inode位图到分区的inode_bitmap.bits
    ide_read(hd, sb_buf->inode_bitmap_lba, cur_part->inode_bitmap.bits, sb_buf->inode_bitmap_sects);
    
    list_init(&cur_part->open_inodes);
    printk("mount %s done!\n", part->name);

    //此处返回true是为了迎合主调函数list_traversal的实现，与函数本身功能无关。
    //只有返回true时list_traversal才会停止遍历，减少了后面元素无意义的遍历
    return true;
  }
  return false;     //使list_traversal继续遍历
}

//格式化分区，也就是初始化分区的元信息，创建文件系统
//接受1个参数，待创建文件系统的分区part。为方便实现，一个块大小是一扇区
//创建文件系统就是创建文件系统所需要的元信息，这包括超级块位置及大小、空闲块位图的位置及大小、
//inode位图的位块置及大小、inode数组的位置及大小、空闲块起始地址、根目录起始地址。创建步骤如下：
//（1）根据分区part大小，计算分区文件系统各元信息需要的扇区数及位置。
//（2）在内存中创建超级，将以上步骤计算的元信息写入超级块。
//（3）将超级块写入磁盘。
//（4）将元信息写入磁盘上各自的位置。
//（5）将根目录写入磁盘。
static void partition_format(struct partition* part) {
  //blocks_bitmap_init（为方便实现，一个块大小是一扇区）
  //操作系统引导块就是操作系统引导记录OBR所在的地址，即操作系统引导扇区，它位于各分区最开始的扇区
  //的引导块未使用，因此无所谓大小，但依然要保留其占位。
  uint32_t boot_sector_sects = 1;   //为引导块占用的扇区数赋值
  //超级块固定存储在各分区的第2个扇超级块逻辑结构区
  uint32_t super_block_sects = 1;   //为超级块占用的扇区数赋值
  //I结点位图占用的扇区数，最多支持4096个文件   经过宏DIV_ROUND_UP计算后inode_bitmap_sects的值为1
  uint32_t inode_bitmap_sects = DIV_ROUND_UP(MAX_FILES_PER_PART, BITS_PER_SECTOR);
  //inode数组占用的扇区数
  uint32_t inode_table_sects = DIV_ROUND_UP((sizeof(struct inode) * MAX_FILES_PER_PART), SECTOR_SIZE);
  //使用的扇区数
  uint32_t used_sects = boot_sector_sects + super_block_sects + inode_bitmap_sects + inode_table_sects;
  uint32_t free_sects = part->sec_cnt - used_sects;   //空闲块数量

  //************** 简单处理块位图占据的扇区数 ***************
  uint32_t block_bitmap_sects;
  //得到了空闲块位图block_bitmap占用的扇区数
  block_bitmap_sects = DIV_ROUND_UP(free_sects, BITS_PER_SECTOR);
  //block_bitmap_bit_len是位图中位的长度，也是可用块的数量
  uint32_t block_bitmap_bit_len = free_sects - block_bitmap_sects;
  //得到空闲块位图最终占用的扇区数
  block_bitmap_sects = DIV_ROUND_UP(block_bitmap_bit_len, BITS_PER_SECTOR);

  //超级块初始化
  struct super_block sb;    //超级块是512字节，栈够用
  sb.magic = 0x19590318;
  sb.sec_cnt = part->sec_cnt;
  sb.inode_cnt = MAX_FILES_PER_PART;
  sb.part_lba_base = part->start_lba;

  //第0块是引导块，第1块是超级块
  sb.block_bitmap_lba = sb.part_lba_base + 2;
  sb.block_bitmap_sects = block_bitmap_sects;

  sb.inode_bitmap_lba = sb.block_bitmap_lba + sb.block_bitmap_sects;
  sb.inode_bitmap_sects = inode_bitmap_sects;

  sb.inode_table_lba = sb.inode_bitmap_lba + sb.inode_bitmap_sects;
  sb.inode_table_sects = inode_table_sects;

  sb.data_start_lba = sb.inode_table_lba + sb.inode_table_sects;
  sb.root_inode_no = 0;   //根目录的inode编号为0
  sb.dir_entry_size = sizeof(struct dir_entry);   //目录项尺寸

  printk("%s info:\n", part->name);
  printk("   magic:0x%x\n   part_lba_base:0x%x\n   all_sectors:0x%x\n   inode_cnt:0x%x\n   block_bitmap_lba:0x%x\n   block_bitmap_sectors:0x%x\n   inode_bitmap_lba:0x%x\n   inode_bitmap_sectors:0x%x\n   inode_table_lba:0x%x\n   inode_table_sectors:0x%x\n   data_start_lba:0x%x\n", sb.magic, sb.part_lba_base, sb.sec_cnt, sb.inode_cnt, sb.block_bitmap_lba, sb.block_bitmap_sects, sb.inode_bitmap_lba, sb.inode_bitmap_sects, sb.inode_table_lba, sb.inode_table_sects, sb.data_start_lba);

  //获取分区part自己所属的硬盘hd
  struct disk *hd = part->my_disk;

  //1将超级块写入本分区的1扇区  跨过引导扇区，把超级块写入引导扇区后面的扇区中
  ide_write(hd, part->start_lba+1, &sb, 1);
  printk("super_block_lba:0x%x\n", part->start_lba + 1);

  //空闲块位图、inode数组位图等占用的扇区数较大（好几百扇区），所以这里不便用局部变量来保存它们了，
  //应该从堆中申请内存获取缓冲区，最好是找出数据量最大的元信息，用其尺寸作为申请的内存大小。
  //找出数据量最大的元信息，用其尺寸做存储缓冲区
  uint32_t buf_size = (sb.block_bitmap_sects >= sb.inode_bitmap_sects ? sb.block_bitmap_sects : sb.inode_bitmap_sects);
  buf_size = (buf_size >= sb.inode_table_sects ? buf_size : sb.inode_table_sects) * SECTOR_SIZE;

  //申请的内存由内存管理系统清0后返回
  uint8_t *buf = (uint8_t *)sys_malloc(buf_size);
  
  //2将块位图初始化并写入sb.block_bitmap_lba
  //初始化块位图block_bitmap
  buf[0] |= 0x01;   //第0个块预留给根目录，位图中先占位   把第0个空闲块作为根目录，因此我们需要在空闲块位图中将第0位置1
  uint32_t block_bitmap_last_byte = block_bitmap_bit_len / 8;
  uint32_t block_bitmap_last_bit = block_bitmap_bit_len % 8;
  //last_size是位图所在最后一个扇区中，不足一扇区的其余部分
  uint32_t last_size = SECTOR_SIZE - (block_bitmap_last_byte % SECTOR_SIZE);
  //1先将位图最后一字节到其所在的扇区的结束全置为1，即超出实际块数的部分直接置为已占用
  memset(&buf[block_bitmap_last_byte], 0xff, last_size);
  //2再将上一步中覆盖的最后一字节内的有效位重新置0
  uint8_t bit_idx = 0;
  while (bit_idx <= block_bitmap_last_bit) {
    buf[block_bitmap_last_byte] &= ~(1 << bit_idx++);
  }
  ide_write(hd, sb.block_bitmap_lba, buf, sb.block_bitmap_sects);

  //3将inode位图初始化并写入sb.inode_bitmap_lba
  //先清空缓冲区
  memset(buf, 0, buf_size);
  buf[0] |= 0x1;    //第0个inode分给了根目录
  //由于inode_table中共4096个inode，位图inode_bitmap正好占用1扇区，即inode_bitmap_sects等于1，
  //所以位图中的位全都代表inode_table中的inode，无需再像block_bitmap那样单独处理最后一扇区的剩余部分，
  //inode_bitmap所在的扇区中没有多余的无效位
  ide_write(hd, sb.inode_bitmap_lba, buf, sb.inode_bitmap_sects);

  //4将inode数组初始化并写入sb.inode_table_lba
  //准备写inode_table中的第0项,即根目录所在的inode
  memset(buf, 0, buf_size);
  struct inode *i = (struct inode *)buf;
  i->i_size = sb.dir_entry_size * 2;    //.和.. i_size表示此目录下所有目录项的大小之和
  i->i_no = 0;    //根目录占inode数组中第0个inode
  i->i_sectors[0] = sb.data_start_lba;

  //由于上面的memset，i_sectors数组的其他元素都初始化为0
  ide_write(hd, sb.inode_table_lba, buf, sb.inode_table_sects);

  //5将根目录写入sb.data_start_lba
  memset(buf, 0, buf_size);
  struct dir_entry *p_de = (struct dir_entry *)buf;

  //初始化当前目录"."
  memcpy(p_de->filename, ".", 1);
  p_de->i_no = 0;
  p_de->f_type = FT_DIRECTORY;
  p_de++;

  //初始化当前目录父目录".."
  memcpy(p_de->filename, "..", 2);
  p_de->i_no = 0;   //根目录的父目录依然是根目录自己
  p_de->f_type = FT_DIRECTORY;

  //sb.data_start_lba已经分配给了根目录，里面是根目录的目录项
  ide_write(hd, sb.data_start_lba, buf, 1);

  printk("root_dir_lba:0x%x\n", sb.data_start_lba);
  printk("%s format done\n", part->name);
  sys_free(buf);
}

//将最上层路径名称解析出来
//接受2个参数，pathname是字符串形式的路径及文件名，name_store是主调函数提供的缓冲区，
//用于存储最上层路径名，此函数功能是将最上层路径名称解析出来存储到name_store中，
//调用结束后返回除顶层路径之外的子路径字符串的地址
char *path_parse(char *pathname, char *name_store) {
  if (pathname[0] == '/') {   //根目录不需要单独解析
  //路径中出现1个或多个连续的字符'/'，将这些'/'跳过，如"///a/b"
  //任何时候路径中最左边的'/'都表示根目录，根目录不需要单独解析，因为它是已知的，并且已经被提前打开了。
  //其次，路径中的'/'仅表示路径分隔符，我们并不关心它，只关心路径分隔符之间的路径名，
  //因此无论'/'表示根目录，还是路径分隔符，我们始终要跨过路径中最左边的'/'。
    while (*(++pathname) == '/');
  }

  //开始一般的路径解析
  while (*pathname != '/' && *pathname != 0) {
    *name_store++ = *pathname++;
  }

  if (pathname[0] == 0) {   //若路径字符串为空，则返回NULL
    return NULL;
  }
  return pathname;
}

//返回路径深度，比如/a/b/c，深度为3
//接受1个参数，pathname表示待分析的路径。函数功能是返回路径深度
int32_t path_depth_cnt(char *pathname) {
  ASSERT(pathname != NULL);
  char *p = pathname;
  char name[MAX_FILE_NAME_LEN];   //用于path_parse的参数做路径解析
  uint32_t depth = 0;

  //解析路径，从中拆分出各级名称
  p = path_parse(p, name);
  while (name[0]) {
    depth++;
    memset(name, 0, MAX_FILE_NAME_LEN);
    if (p) {      //如果p不等于NULL，继续分析路径
      p = path_parse(p, name);
    }
  }

  return depth;
}

//搜索文件pathname，若找到则返回其inode号，否则返回-1
//接受2个参数，被检索的文件pathname和路径搜索记录指针searched_record，
//功能是搜索文件pathname，若找到则返回其inode号，否则返回−1
//其中参数 pathname 是全路径，即从根目录开始的路径。参数searched_record由主调函数提供
//该结构中的数据由函数search_file填充，其中的成员parent_dir记录的是待查找目标（文件或目录）的直接父目录，
//原因是主调函数通常需要获取目标的父目录作为操作对象，比如将来创建文件时，需要知道在哪个目录中创建文件，
//因此所有调用search_file的主调函数记得释放目录searched_record->parent_dir，避免内存泄漏。
static int search_file(const char *pathname, struct path_search_record *searched_record) {
  //如果待查找的是根目录，为避免下面无用的查找，直接返回已知根目录信息
  if (!strcmp(pathname, "/") || !strcmp(pathname, "/.") || !strcmp(pathname, "/..")) {
    searched_record->parent_dir = &root_dir;
    searched_record->file_type = FT_DIRECTORY;
    searched_record->searched_path[0] = 0;      //搜索路径置空
    return 0;
  }

  uint32_t path_len = strlen(pathname);
  //保证pathname至少是这样的路径/x，且小于最大长度
  ASSERT(pathname[0] == '/' && path_len > 1 && path_len < MAX_PATH_LEN);
  char* sub_path = (char*)pathname;   //指向路径名pathname
  struct dir* parent_dir = &root_dir;	  //指向根目录
  struct dir_entry dir_e;     //声明了目录项dir_e

  //记录路径解析出来的各级名称,如路径"/a/b/c",数组name每次的值分别是"a","b","c"
  char name[MAX_FILE_NAME_LEN] = {0};   //用来存储路径解析中的各层路径名

  searched_record->parent_dir = parent_dir;
  searched_record->file_type = FT_UNKNOWN;
  //根目录开始解析路径，因此初始化parent_inode_no为根目录的inode编号0。
  uint32_t parent_inode_no = 0;   //父目录的inode号
  
  //路径解析，path_parse返回后，最上层的路径名会存储在name中，返回值存入sub_path
  sub_path = path_parse(sub_path, name);
  while (name[0]) {	    //若第一个字符就是结束符,结束循环，否则路径解析尚未结束
    //记录查找过的路径,但不能超过searched_path的长度512字节
    ASSERT(strlen(searched_record->searched_path) < 512);

    //记录已存在的父目录
    //解析过的路径都会追加到searched_record->searched_path中
    //第一次执行时所添加的'/'表示根目录，后续循环中添加的'/'是路径分隔符
    strcat(searched_record->searched_path, "/");
    strcat(searched_record->searched_path, name);

    //在所给的目录中查找文件
    //由于是先调用path_parse解析路径，再调用search_dir_entry去验证路径是否存在，
    //因此searched_record->searched_path中的最后一级目录未必存在，其前的所有路径都是存在的
    if (search_dir_entry(cur_part, parent_dir, name, &dir_e)) { //判断解析出来的上层路径name是否在父目录parent_dir中存在
      memset(name, 0, MAX_FILE_NAME_LEN);
      //若sub_path不等于NULL,也就是未结束路径解析，继续拆分路径
      if (sub_path) {
        sub_path = path_parse(sub_path, name);
      }

      if (FT_DIRECTORY == dir_e.f_type) {     //如果被打开的是目录
        //将父目录的inode编号赋值给变量parent_inode_no，此变量用于备份父目录的inode编号，
        //它会在最后一级路径为目录的情况下用到
        parent_inode_no = parent_dir->inode->i_no;
        dir_close(parent_dir);    //关闭父目录  第一次关闭根目录，第二次关闭上一次打开的那个目录
        //把目录name打开，重新为parent_dir赋值
        parent_dir = dir_open(cur_part, dir_e.i_no);  //更新父目录
        searched_record->parent_dir = parent_dir;     //更新搜索记录中的父目录
        continue;
      } else if (FT_REGULAR == dir_e.f_type) {	  //若是普通文件
        searched_record->file_type = FT_REGULAR;
        return dir_e.i_no;      //回普通文件name的inode编号
      }
    } else {		   //若找不到,则返回-1
      //找不到目录项时,要留着parent_dir不要关闭,若是创建新文件的话需要在parent_dir中创建
      return -1;
    }
  }

  //执行到此
  //（1）路径pathname已经被完整地解析过了，各级都存在。
  //（2）pathname的最后一层路径不是普通文件，而是目录。
  //结论是待查找的目标是目录，如“/a/b/c”，c是目录，不是普通文件。此时searched_record->parent_dir
  //是路径pathname中的最后一级目录c，并不是倒数第二级的父目录b，我们在任何时候都应该使
  //searched_record->parent_dir是被查找目标的直接父目录，也就是说，无论目标是普通文件，还是目录，
  //searched_record->parent_dir中记录的都应该是目录b。
  //因此我们需要把searched_record->parent_dir重新更新为父目录b。

  //执行到此,必然是遍历了完整路径并且查找的文件或目录只有同名目录存在
  dir_close(searched_record->parent_dir);	      

  //保存被查找目录的直接父目录
  searched_record->parent_dir = dir_open(cur_part, parent_inode_no);	   
  searched_record->file_type = FT_DIRECTORY;
  return dir_e.i_no;    //返回目录的inode编号
}

//打开或创建文件成功后，返回文件描述符，否则返回-1
//接受2个参数，pathname是待打开的文件，其为绝对路径，flags是打开标识，
//其值便是之前在fs.h头文件中提前放入的enum oflags。函数功能是打开或创建文件成功后，
//返回文件描述符，即pcb中fd_table中的下标，否则返回−1。
int32_t sys_open(const char *pathname, uint8_t flags) {
  //对目录要用dir_open，这里只有open文件
  //sys_open只支持文件打开，不支持目录打开，因此程序开头判断pathname是否为目录
  if (pathname[strlen(pathname) - 1] == '/') {
    printk("can`t open a directory %s\n",pathname);
    return -1;
  }
  //限制flags的值在O_RDONLY | O_WRONLY | O_RDWR | O_CREAT之内
  ASSERT(flags <= 7);
  int32_t fd = -1;//默认为找不到
  //用来记录文件查找时所遍历过的目录,可以通过该路径的长度来判断查找是否成功。举个例子，
  //若查找目标文件c，它的绝对路径是“/a/b/c”，查找时若发现b目录不存在，
  //存入path_search_ record.searched_path的内容便是“/a/b”，
  //若按照此路径找到了c，path_search_record.searched_path的值便是完整路径“/a/b/c”。
  struct path_search_record searched_record;
  memset(&searched_record, 0, sizeof(struct path_search_record)); //将searched_record清0

  //记录目录深度，帮助判断中间某个目录不存在的情况
  uint32_t pathname_depth = path_depth_cnt((char *)pathname);

  //先检查文件是否存在
  int inode_no = search_file(pathname, &searched_record);
  bool found = inode_no != -1 ? true : false;

  if (searched_record.file_type == FT_DIRECTORY) {
    printk("can`t open a direcotry with open(), use opendir() to instead\n");
    dir_close(searched_record.parent_dir);
    return -1;
  }

  //求访问过的路径深度  
  uint32_t path_searched_depth = path_depth_cnt(searched_record.searched_path);

  //先判断是否把pathname的各层目录都访问到了，即是否在某个中间目录就失败了
  if (pathname_depth != path_searched_depth) {
    //说明并没有访问到全部的路径，某个中间目录是不存在的
    printk("cannot access %s: Not a directory, subpath %s is’t exist\n", \
    pathname, searched_record.searched_path);
    dir_close(searched_record.parent_dir);
    return -1;
  }

  //若是在最后一个路径上没找到，并且并不是要创建文件，直接返回-1
  if (!found && !(flags & O_CREAT)) {   //若目标文件未找到，并且flags不包含O_CREAT
    printk("in path %s, file %s is`t exist\n",searched_record.searched_path, \
    (strrchr(searched_record.searched_path, '/') + 1));
    dir_close(searched_record.parent_dir);
    return -1;
  }
  //若找到了文件并且flags包含O_CREAT，这说明想创建的文件名已存在，相同目录下
  //不允许同名文件存在，因此输出报错并关闭目录，返回−1。 
  else if(found && flags & O_CREAT) {   //若要创建的文件已存在
    printk("%s has already exist!\n", pathname);
    dir_close(searched_record.parent_dir);
    return -1;
  }

  switch (flags & O_CREAT) {
    case O_CREAT:
      printk("creating file\n");
      fd = file_create(searched_record.parent_dir, (strrchr(pathname, '/') + 1), flags);
      dir_close(searched_record.parent_dir);
      break;    
    default:    //其余情况均为打开已存在文件，O_RDONLY,O_WRONLY,O_RDWR
      fd = file_open(inode_no, flags);
  }

  //此fd是指任务pcb->fd_table数组中的元素下标，并不是指全局file_table中的下标
  return fd;
}

//将文件描述符转化为文件表的下标
//接受1个参数，文件描述符local_fd，功能是将文件描述符转化为文件表的下标。
uint32_t fd_local2global(uint32_t local_fd) {
  struct task_struct *cur = running_thread();
  int32_t global_fd = cur->fd_table[local_fd];
  ASSERT(global_fd >= 0 && global_fd < MAX_FILE_OPEN);
  return (uint32_t)global_fd;
}

//关闭文件描述符fd指向的文件,成功返回0,否则返回-1
//接受1个参数，文件描述符fd，功能是关闭文件描述符fd指向的文件，成功返回0，否则返回−1。
int32_t sys_close(int32_t fd) {
  int32_t ret = -1;   //返回值默认为-1,即失败
  if (fd > 2) {
    uint32_t global_fd = fd_local2global(fd);
    if (is_pipe(fd)) {
      //如果此管道上的描述符都被关闭,释放管道的环形缓冲区
      if (--file_table[global_fd].fd_pos == 0) {  //判断关闭的文件描述符是否是管道
        mfree_page(PF_KERNEL, file_table[global_fd].fd_inode, 1);
        file_table[global_fd].fd_inode = NULL;
      }
      ret = 0;
    } else {
      ret = file_close(&file_table[global_fd]);
    }
    running_thread()->fd_table[fd] = -1;  //使该文件描述符位可用
  }
  return ret;
}

//将buf中连续count个字节写入文件描述符fd,成功则返回写入的字节数,失败返回-1
//接受3个参数，文件描述符fd、数据所在缓冲区buf、写入的字节数count
int32_t sys_write(int32_t fd, const void* buf, uint32_t count) {
  if (fd < 0) {
    printk("sys_write: fd error\n");
    return -1;
  }
  if (fd == stdout_no) {
    //标准输出有可能被重定向为管道缓冲区, 因此要判断
    if (is_pipe(fd)) {
      return pipe_write(fd, buf, count);
    } else {
      char tmp_buf[1024] = {0};
      memcpy(tmp_buf, buf, count);
      console_put_str(tmp_buf);
      return count;
    }
  } else if (is_pipe(fd)) {   //若是管道就调用管道的方法
    return pipe_write(fd, buf, count);
  } else {
    uint32_t _fd = fd_local2global(fd);   //获取文件描述符fd对应于文件表中的下标_fd
    struct file* wr_file = &file_table[_fd];
    if (wr_file->fd_flag & O_WRONLY || wr_file->fd_flag & O_RDWR) {   //判断其flag
      uint32_t bytes_written = file_write(wr_file, buf, count);
      return bytes_written;
    } else {
      console_put_str("sys_write: not allowed to write file without flag O_RDWR or O_WRONLY\n");
      return -1;
    }
  }
}

//从文件描述符fd指向的文件中读取count个字节到buf,若成功则返回读出的字节数,到文件尾则返回-1
int32_t sys_read(int32_t fd, void* buf, uint32_t count) {
  ASSERT(buf != NULL);
  int32_t ret = -1;
  uint32_t global_fd = 0;
  if (fd < 0 || fd == stdout_no || fd == stderr_no) {
    printk("sys_read: fd error\n");
  } else if (fd == stdin_no) {
    //标准输入有可能被重定向为管道缓冲区, 因此要判断
    if (is_pipe(fd)) {
      ret = pipe_read(fd, buf, count);
    } else {
      //若发现fd是stdin_no，下面就通过while和ioq_getchar(&kbd_buf)，
      //每次从键盘缓冲区kbd_buf中获取1个字符，直到获取了count个字符为止。
      char* buffer = buf;
      uint32_t bytes_read = 0;
      while (bytes_read < count) {
        *buffer = ioq_getchar(&kbd_buf);
        bytes_read++;
        buffer++;
        if (*(buffer -1) == '\r') {
          break;
        }
      }
      ret = (bytes_read == 0 ? -1 : (int32_t)bytes_read);
    }
  } else if (is_pipe(fd)) { //若是管道就调用管道的方法
    ret = pipe_read(fd, buf, count);
  } else {
    global_fd = fd_local2global(fd);
    ret = file_read(&file_table[global_fd], buf, count);
  }
  return ret;
}

//重置用于文件读写操作的偏移指针成功时返回新的偏移量，出错时返回-1
//接受3个参数，文件描述符fd、偏移量offset、参数位置whence，功能是重置用于文件读写操作的偏移指针，
//成功时返回新的偏移量，出错时返回−1。
int32_t sys_lseek(int32_t fd, int32_t offset, uint8_t whence) {
  if (fd < 0) {
    printk("sys_lseek: fd error\n");
    return -1;
  }
  ASSERT(whence > 0 && whence < 4);
  uint32_t _fd = fd_local2global(fd);
  struct file *pf = &file_table[_fd];
  int32_t new_pos = 0;      //新的偏移量必须位于文件大小之内
  int32_t file_size = (int32_t)pf->fd_inode->i_size;
  switch(whence) {
    //SEEK_SET新的读写位置是相对于文件开头再增加offset个位移量
    case SEEK_SET:
      new_pos = offset;
      break;
    //SEEK_CUR新的读写位置是相对于当前的位置增加offset个位移量
    case SEEK_CUR:    //offse可正可负
      new_pos = (int32_t)pf->fd_pos + offset;
      break;
    //SEEK_END新的读写位置是相对于文件尺寸再增加offset个位移量
    case SEEK_END:    //此情况下，offset 应该为负值
      new_pos = file_size + offset;
      break;
  }
  if (new_pos < 0 || new_pos > (file_size - 1)) {
    return -1;
  }
  pf->fd_pos = new_pos;
  return pf->fd_pos;
}

//删除文件（非目录），成功返回0，失败返回-1
//接受1个参数，文件绝对路径名pathname，删除文件（非目录），成功返回0，失败返回−1。
int32_t sys_unlink(const char *pathname) {
  ASSERT(strlen(pathname) < MAX_PATH_LEN);

  //先检查待删除的文件是否存在
  struct path_search_record searched_record;
  memset(&searched_record, 0, sizeof(struct path_search_record));
  //检查文件pathname是否存在，如果不存在，则输出提示并返回−1。
  int inode_no = search_file(pathname, &searched_record);
  ASSERT(inode_no != 0);
  if (inode_no == -1) {
    printk("file %s not found!\n", pathname);
    dir_close(searched_record.parent_dir);
    return -1;
  }
  //若只存在同名的目录，提示不能用unlink删除目录，只能用rmdir函数（将来实现）并返回−1。
  if (searched_record.file_type == FT_DIRECTORY) {
    printk("can't delete directory with unlink(),use rmdir() to instead\n");
    dir_close(searched_record.parent_dir);
    return -1;
  }

  //检查是否在已打开文件列表（文件表）中
  //文件表中检索待删除的文件，如果文件在文件表中存在，这说明该文件正被打开，不能删除。
  uint32_t file_idx = 0;
  while (file_idx < MAX_FILE_OPEN) {
    if (file_table[file_idx].fd_inode != NULL && (uint32_t)inode_no == file_table[file_idx].fd_inode->i_no) {
      break;
    }
    file_idx++;
  }
  if (file_idx < MAX_FILE_OPEN) {
    dir_close(searched_record.parent_dir);
    printk("file %s is in use, not allow to delete!\n", pathname);
    return -1;
  }
  ASSERT(file_idx == MAX_FILE_OPEN);

  //为delete_dir_entry申请缓冲区
  void *io_buf = sys_malloc(SECTOR_SIZE + SECTOR_SIZE); //为即将调用的delete_dir_entry函数申请缓冲区
  if (io_buf == NULL) {
    dir_close(searched_record.parent_dir);
    printk("sys_unlink: malloc for io_buf failed\n");
    return -1;
  }

  struct dir *parent_dir = searched_record.parent_dir;
  delete_dir_entry(cur_part, parent_dir, inode_no, io_buf);   //调用函数delete_dir_entry删除目录项
  inode_release(cur_part, inode_no);    //调用inode_release释放inode
  sys_free(io_buf);
  dir_close(searched_record.parent_dir);    //调用dir_close关闭pathname所在的目录
  return 0;       //成功删除文件
}

//创建目录所涉及的工作包括。
//（1）确认待创建的新目录在文件系统上不存在。
//（2）为新目录创建inode。
//（3）为新目录分配1个块存储该目录中的目录项。
//（4）在新目录中创建两个目录项“.”和“..”，这是每个目录都必须存在的两个目录项。
//（5）在新目录的父目录中添加新目录的目录项。
//（6）将以上资源的变更同步到硬盘。

//创建目录pathname,成功返回0,失败返回-1
//支持1个参数，路径名pathname，功能是创建目录pathname，成功返回0，失败返回−1。
int32_t sys_mkdir(const char* pathname) {
  //创建目录也是由多个步骤完成的，因此创建目录的工作是个事务，具有原子性，即要么所有步骤都完成，要
  //么一个都不做，若其中某个步骤失败，必须将之前完成的操作回滚到之前的状态。
  uint8_t rollback_step = 0;      //用于操作失败时回滚各资源状态
  void *io_buf = sys_malloc(SECTOR_SIZE * 2);   //申请2扇区大小的缓冲区给io_buf
  if (io_buf == NULL) {
    printk("sys_mkdir: sys_malloc for io_buf failed\n");
    return -1;
  }

  struct path_search_record searched_record;
  memset(&searched_record, 0, sizeof(struct path_search_record));
  int inode_no = -1;
  //检索pathname，如果找到同名文件pathname，search_file会返回其inode编号，否则会返回−1
  inode_no = search_file(pathname, &searched_record);
  if (inode_no != -1) {     //如果找到了同名目录或文件,失败返回
    printk("sys_mkdir: file or directory %s exist!\n", pathname);
    rollback_step = 1;
    goto rollback;
  } else {	        //若未找到,也要判断是在最终目录没找到还是某个中间目录不存在
    //如果未找到同名文件，这也不能贸然创建目录，因为待创建的目录有可能并不是在最后一级目录中不存在，
    //很可能是某个中间路径就不存在，比如创建目录“/a/b”，有可能a目录就不存在。
    //对于中间目录不存在的情况，我们就像Linux一样，给出提示后拒绝创建目录
    uint32_t pathname_depth = path_depth_cnt((char*)pathname);
    uint32_t path_searched_depth = path_depth_cnt(searched_record.searched_path);
    //先判断是否把pathname的各层目录都访问到了,即是否在某个中间目录就失败了
    if (pathname_depth != path_searched_depth) {  //说明并没有访问到全部的路径,某个中间目录是不存在的
      printk("sys_mkdir: can`t access %s, subpath %s is`t exist\n", pathname, searched_record.searched_path);
      rollback_step = 1;
      goto rollback;
    }
  }

  struct dir *parent_dir = searched_record.parent_dir;  //指向被创建目录所在的父目录
  //目录名称后可能会有字符'/',所以最好直接用searched_record.searched_path,无'/'
  char *dirname = strrchr(searched_record.searched_path, '/') + 1;

  inode_no = inode_bitmap_alloc(cur_part);    //在inode位图中分配inode
  if (inode_no == -1) {
    printk("sys_mkdir: allocate inode failed\n");
    rollback_step = 1;
    goto rollback;
  }

  struct inode new_dir_inode;
  inode_init(inode_no, &new_dir_inode);   //初始化i结点

  uint32_t block_bitmap_idx = 0;    //用来记录block对应于block_bitmap中的索引
  int32_t block_lba = -1;
  //为目录分配一个块,用来写入目录.和..
  block_lba = block_bitmap_alloc(cur_part);   //分配1个块
  if (block_lba == -1) {
    printk("sys_mkdir: block_bitmap_alloc for create directory failed\n");
    rollback_step = 2;
    goto rollback;
  }
  new_dir_inode.i_sectors[0] = block_lba;   //将块地址写入目录inode的i_sectors[0]中
  //每分配一个块就将位图同步到硬盘
  block_bitmap_idx = block_lba - cur_part->sb->data_start_lba;
  ASSERT(block_bitmap_idx != 0);
  bitmap_sync(cur_part, block_bitmap_idx, BLOCK_BITMAP);  //将块位图同步到硬盘
  
  //将当前目录的目录项'.'和'..'写入目录
  memset(io_buf, 0, SECTOR_SIZE * 2);   //清空io_buf
  struct dir_entry* p_de = (struct dir_entry*)io_buf;
  
  //新建目录项“.”和“..”并同步到硬盘

  //初始化当前目录"."
  memcpy(p_de->filename, ".", 1);
  p_de->i_no = inode_no ;
  p_de->f_type = FT_DIRECTORY;

  p_de++;
  //初始化当前目录".."
  memcpy(p_de->filename, "..", 2);
  p_de->i_no = parent_dir->inode->i_no;
  p_de->f_type = FT_DIRECTORY;
  ide_write(cur_part->my_disk, new_dir_inode.i_sectors[0], io_buf, 1);

  new_dir_inode.i_size = 2 * cur_part->sb->dir_entry_size;    //初始化目录的尺寸

  //在父目录中添加自己的目录项
  struct dir_entry new_dir_entry;
  memset(&new_dir_entry, 0, sizeof(struct dir_entry));
  //初始化目录项的内容到new_dir_entry中
  create_dir_entry(dirname, inode_no, FT_DIRECTORY, &new_dir_entry);
  memset(io_buf, 0, SECTOR_SIZE * 2); //清空io_buf
  //把dirname的目录项new_dir_entry写入父目录parent_dir中。
  if (!sync_dir_entry(parent_dir, &new_dir_entry, io_buf)) {  //sync_dir_entry中将block_bitmap通过bitmap_sync同步到硬盘
    printk("sys_mkdir: sync_dir_entry to disk failed!\n");
    rollback_step = 2;
    goto rollback;
  }

  //父目录的inode同步到硬盘
  memset(io_buf, 0, SECTOR_SIZE * 2);
  inode_sync(cur_part, parent_dir->inode, io_buf);

  //将新创建目录的inode同步到硬盘
  memset(io_buf, 0, SECTOR_SIZE * 2);
  inode_sync(cur_part, &new_dir_inode, io_buf);

  //将inode位图同步到硬盘
  bitmap_sync(cur_part, inode_no, INODE_BITMAP);

  sys_free(io_buf);   //释放缓冲区io_buf

  //关闭所创建目录的父目录
  dir_close(searched_record.parent_dir);
  return 0;

//创建文件或目录需要创建相关的多个资源,若某步失败则会执行到下面的回滚步骤
rollback:     //因为某步骤操作失败而回滚
  switch (rollback_step) {
    case 2:
      //恢复inode位图
      bitmap_set(&cur_part->inode_bitmap, inode_no, 0); //如果新文件的inode创建失败,之前位图中分配的inode_no也要恢复 
    case 1:
      //关闭所创建目录的父目录
      dir_close(searched_record.parent_dir);
      break;
  }
  sys_free(io_buf);
  return -1;
}

//目录打开成功后返回目录指针，失败返回NULL
//接受一个参数name，功能是打开目录name，成功后返回目录指针，失败返回NULL。
struct dir *sys_opendir(const char *name) {
  //根目录的形式有：“/”、“/.”、“/..”，当然按理说“/./..”、“/../..”等都能够表示根目录，
  //但毕竟实属“罕见”，因此暂不考虑它们。
  ASSERT(strlen(name) < MAX_PATH_LEN);
  //如果是根目录'/'，直接返回&root_dir
  if (name[0] == '/' && (name[1] == 0 || name[0] == '.')) {
    return &root_dir;
  }

  //先检查待打开的目录是否存在
  struct path_search_record searched_record;
  memset(&searched_record, 0, sizeof(struct path_search_record));
  int inode_no = search_file(name, &searched_record);
  struct dir *ret = NULL;
  if (inode_no == -1) {   //如果找不到目录，提示不存在的路径
    printk("In %s, sub path %s not exist\n", name, searched_record.searched_path);
  } else {
    if (searched_record.file_type == FT_REGULAR) {
      printk("%s is regular file!\n", name);
    } else if (searched_record.file_type == FT_DIRECTORY) {
      ret = dir_open(cur_part, inode_no);
    }
  }
  dir_close(searched_record.parent_dir);
  return ret;
}

//成功关闭目录p_dir返回0，失败返回-1
int32_t sys_closedir(struct dir *dir) {
  int32_t ret = -1;
  if (dir != NULL) {
    dir_close(dir);
    ret = 0;
  }
  return ret;
}

//读取目录dir的1个目录项，成功后返回其目录项地址，到目录尾时或出错时返回NULL
struct dir_entry *sys_readdir(struct dir *dir) {
  ASSERT(dir != NULL);
  return dir_read(dir);
}

//把目录dir的指针dir_pos置0
void sys_rewinddir(struct dir *dir) {
  dir->dir_pos = 0;
}

//删除空目录，成功时返回0，失败时返回-1
//接受1个参数，待删除的目录pathname，功能是删除空目录pathname，成功时返回0，失败时返回−1。
int32_t sys_rmdir(const char *pathname) {
  //先检查待删除的文件是否存在
  struct path_search_record searched_record;
  memset(&searched_record, 0, sizeof(struct path_search_record));
  int inode_no = search_file(pathname, &searched_record); //判断目录是否存在
  ASSERT(inode_no != 0);
  int retval = -1;    //默认返回值
  if (inode_no == -1) {
    printk("In %s, sub path %s not exist\n", pathname, searched_record.searched_path);
  } else {
    if (searched_record.file_type == FT_REGULAR) {  //同名的普通文件
      printk("%s is regular file!\n", pathname);
    } else {
      struct dir *dir = dir_open(cur_part, inode_no);
      if (!dir_is_empty(dir)) {     //判断其是否为空，非空目录不可删除
        printk("dir %s is not empty, it is not allowed to delete a nonempty directory!\n", pathname);
      } else {
        if (!dir_remove(searched_record.parent_dir, dir)) {
          retval = 0;
        }
      }
      dir_close(dir);
    }
  }
  dir_close(searched_record.parent_dir);
  return retval;
}

//获得父目录的inode编号
//get_parent_dir_inode_nr接受2个参数，子目录inode编号child_inode_nr、缓冲区io_buf，功能是获得父目录的inode编号。
static uint32_t get_parent_dir_inode_nr(uint32_t child_inode_nr, void *io_buf) {
  struct inode *child_dir_inode = inode_open(cur_part, child_inode_nr);   //获得子目录的inode
  //目录中的目录项".."中包括父目录inode编号，".."位于目录的第0块
  uint32_t block_lba = child_dir_inode->i_sectors[0];
  ASSERT(block_lba >= cur_part->sb->data_start_lba);
  inode_close(child_dir_inode);
  ide_read(cur_part->my_disk, block_lba, io_buf, 1);
  struct dir_entry *dir_e = (struct dir_entry *)io_buf;
  //第0个目录项是"."，第1个目录项是".."
  ASSERT(dir_e[1].i_no < 4096 && dir_e[1].f_type == FT_DIRECTORY);
  return dir_e[1].i_no;   //返回..即父目录的inode编号
}

//在inode编号为p_inode_nr的目录中查找inode编号为c_inode_nr的子目录的名字，
//将名字存入缓冲区path，成功返回0，失败返-1
//接受4个参数，父目录inode编号p_inode_nr、子目录inode编号c_inode_nr、存储路径的缓冲区path、
//硬盘读写缓冲区io_buf，功能是在inode编号为p_inode_nr的目录中查找inode编号为c_inode_nr的子目录，
//将子目录的名字存入缓冲区path，成功返回0，失败返−1。
static int get_child_dir_name(uint32_t p_inode_nr, uint32_t  c_inode_nr, char *path, void *io_buf) {
  //get_child_dir_name每次只获得一层目录的名称
  struct inode *parent_dir_inode = inode_open(cur_part, p_inode_nr);  //打开父目录的inode
  //填充all_blocks，将该目录的所占扇区地址全部写入all_blocks
  uint8_t block_idx = 0;
  uint32_t all_blocks[140] = {0}, block_cnt = 12;
  while (block_idx < 12) {
    all_blocks[block_idx] = parent_dir_inode->i_sectors[block_idx];
    block_idx++;
  }
  if (parent_dir_inode->i_sectors[12]) {
    //若包含了一级间接块表，将其读入all_blocks
    ide_read(cur_part->my_disk, parent_dir_inode->i_sectors[12], all_blocks + 12, 1);
    block_cnt = 140;
  }
  inode_close(parent_dir_inode);

  struct dir_entry *dir_e = (struct dir_entry *)io_buf;
  uint32_t dir_entry_size = cur_part->sb->dir_entry_size;
  uint32_t dir_entrys_per_sec = 512 / dir_entry_size;
  block_idx = 0;
  //遍历所有块
  while (block_idx < block_cnt) {
    if (all_blocks[block_idx]) {    //如果相应块不为空，则读入相应块
      ide_read(cur_part->my_disk, all_blocks[block_idx], io_buf, 1);
      uint8_t dir_e_idx = 0;
      //遍历每个目录项
      while (dir_e_idx < dir_entrys_per_sec) {
        if ((dir_e + dir_e_idx)->i_no == c_inode_nr) {
          strcat(path, "/");
          strcat(path, (dir_e + dir_e_idx)->filename);
          return 0;
        }
        dir_e_idx++;
      }
    }
    block_idx++;
  }
  return -1;
}

//把当前工作目录绝对路径写入buf，size是buf的大小。
//当buf为NULL时，由操作系统分配存储工作路径的空间并返回地址，失败则返回NULL
//sys_getcwd接受两个参数，存储绝对路径的缓冲区buf、缓冲区大小size，
//功能是把当前工作目录的绝对路径写入buf，成功返回buf地址，失败返回NULL。
char *sys_getcwd(char *buf, uint32_t size) {
  //确保buf不为空，若用户进程提供的buf为NULL，系统调用getcwd中要为用户进程通过malloc分配内存
  ASSERT(buf != NULL);    //限制了buf不为空
  void *io_buf = sys_malloc(SECTOR_SIZE);
  if (io_buf == NULL) {
    return NULL;
  }

  struct task_struct *cur_thread = running_thread();
  int32_t parent_inode_nr = 0;
  int32_t child_inode_nr = cur_thread->cwd_inode_nr;  //获得当前任务工作目录的inode编号
  ASSERT(child_inode_nr >= 0 && child_inode_nr < 4096);   //最大支持4096个inode
  if (child_inode_nr == 0) {    //如果child_inode_nr是0，这说明是根目录的inode编号
    buf[0] = '/';
    buf[1] = 0;
    return buf;
  }
  memset(buf, 0, size);
  //用于存储工作目录所在的全路径，即绝对路径，不过从名字上看，它是反转的绝对路径
  char full_path_reverse[MAX_PATH_LEN] = {0};   //用来做全路径缓冲区

  //从下往上逐层找父目录，直到找到根目录为止。
  //当child_inode_nr为根目录的inode编号(0)时停止，即已经查看完根目录中的目录项
  while ((child_inode_nr)) {
    parent_inode_nr = get_parent_dir_inode_nr(child_inode_nr, io_buf);  //获得父目录的inodeb编号
    //或未找到名字，失败退出
    if (get_child_dir_name(parent_inode_nr, child_inode_nr, full_path_reverse, io_buf) == -1) {
      sys_free(io_buf);
      return NULL;
    }
    child_inode_nr = parent_inode_nr;
  }
  ASSERT(strlen(full_path_reverse) <= size);
  //至此full_path_reverse中的路径是反着的，
  //即子目录在前（左），父目录在后（右），现将full_path_reverse中的路径反置
  char *last_slash;     //用于记录字符串中最后一个斜杠地址
  //通过while循环逐层解析目录名，将最终的路径写入buf中。
  while ((last_slash = strrchr(full_path_reverse, '/'))) {
    uint16_t len = strlen(buf);
    strcpy(buf + len, last_slash);
    //在full_path_reverse中添加结束字符，作为下一次执行strcpy中last_slash的边界
    *last_slash = 0;
  }
  sys_free(io_buf);
  return buf;
}

//接受1个参数，新工作目录的绝对路径path，功能是更改当前工作目录为绝对路径path，成功则返回0，失败返回−1。
int32_t sys_chdir(const char *path) {
  int32_t ret = -1;
  struct path_search_record searched_record;
  memset(&searched_record, 0, sizeof(struct path_search_record));
  int inode_no = search_file(path, &searched_record);
  if (inode_no != -1) {
    if (searched_record.file_type == FT_DIRECTORY) {
      running_thread()->cwd_inode_nr = inode_no;
      ret = 0;
    } else {
      printk("sys_chdir: %s is regular file or other!\n", path);
    }
  }
  dir_close(searched_record.parent_dir);
  return ret;
}

//在buf中填充文件结构相关信息，成功时返回0，失败返回-1
//接受2个参数，待获取属性的文件路径path、存储属性的缓冲区buf，
//功能是在buf中填充文件结构相关信息，成功时返回0，失败返回−1。
int32_t sys_stat(const char *path, struct stat *buf) {
  //若直接查看根目录'/'
  if (!strcmp(path, "/") || !strcmp(path, "/.") || !strcmp(path, "/..")) {
    buf->st_filetype = FT_DIRECTORY;
    buf->st_ino = 0;
    buf->st_size = root_dir.inode->i_size;
    return 0;
  }

  int32_t ret = -1;     //默认返回值
  struct path_search_record searched_record;
  memset(&searched_record, 0, sizeof(struct path_search_record)); //清0，否则栈中信息不知道是什么
  int inode_no = search_file(path, &searched_record);
  if (inode_no != -1) {
    struct inode *obj_inode = inode_open(cur_part, inode_no);     //只为获得文件大小
    buf->st_size = obj_inode->i_size;
    inode_close(obj_inode);
    buf->st_filetype = searched_record.file_type;
    buf->st_ino = inode_no;
    ret = 0;
  } else {
    printk("sys_stat: %s not found\n", path);
  }
  dir_close(searched_record.parent_dir);
  return ret;
}

//向屏幕输出一个字符
void sys_putchar(char char_asci) {
   console_put_char(char_asci);
}

//显示系统支持的内部命令
void sys_help(void) {
  printk("\
  buildin commands:\n\
        ls: show directory or file information\n\
        cd: change current work directory\n\
        mkdir: create a directory\n\
        rmdir: remove a empty directory\n\
        rm: remove a regular file\n\
        pwd: show current work directory\n\
        ps: show process information\n\
        clear: clear screen\n\
        shortcut key:\n\
        ctrl+l: clear screen\n\
        ctrl+u: clear input\n\n");
}

//在磁盘上搜索文件系统，若没有则格式化分区创建文件系统
void filesys_init() {
  uint8_t channel_no = 0, dev_no, part_idx = 0;

  //sb_buf用来存储从硬盘上读入的超级块
  struct super_block *sb_buf = (struct super_block *)sys_malloc(SECTOR_SIZE);

  if (sb_buf == NULL) {
    PANIC("alloc memory failed!\n");
  }
  printk("searching filesystem......\n");

  //通过三层循环完成的，最外层循环用来遍历通道，中间层循环用来遍历通道中的硬盘，最内层循环用来遍历硬盘上的所有分区
  while (channel_no < channel_cnt) {
    dev_no = 0;
    while (dev_no < 2) {
      if (dev_no == 0) {  //跨过裸盘os.img
        dev_no++;
        continue;
      }
      struct disk *hd = &(channels[channel_no].devices[dev_no]);
      struct partition *part = hd->prim_parts;
      while (part_idx < 12) {   //4个主分区+8个逻辑
        if (part_idx == 4) {    //开始处理逻辑分区
          part = hd->logic_parts;
        }
        //channels数组是全局变量，默认值为0，disk属于其嵌套结构，partition又为disk的嵌套结构，
        //因此partition中的成员默认也为0。若partition未初始化，则partition中的成员仍为0。
        //part所在的硬盘作为全局数组channels的内嵌成员，全局变量会被初始化为0
        //下面处理存在的分区
        //分区 part 中任意成员的值都会是0，只是我们这里用sec_cnt来判断而已
        if (part->sec_cnt != 0) {   //判断分区是否存在，如果分区存在
          memset(sb_buf, 0, SECTOR_SIZE);
          ide_read(hd, part->start_lba + 1, sb_buf, 1);
          //只支持自己的文件系统，若磁盘上已经有文件系统就不再格式化了
          if (sb_buf->magic == 0x19590318) {
            printk("%s has filesystem\n", part->name);
          } else {    //其他文件系统不支持，一律按无文件系统处理
            printk("formatting %s`s partition %s......\n", hd->name, part->name);
            partition_format(part);
          }
        }
        part_idx++;
        part++;     //下一分区
      }
      dev_no++;     //下一磁盘
    }
    channel_no++;   //下一通道
  }
  sys_free(sb_buf);

  //确定默认操作的分区
  char default_part[8] = "sdb1";
  //挂载分区
  list_traversal(&partition_list, mount_partition, (int)default_part);
  //将当前分区的根目录打开
  open_root_dir(cur_part);

  //初始化文件表
  uint32_t fd_idx = 0;
  while (fd_idx < MAX_FILE_OPEN) {
    file_table[fd_idx++].fd_inode = NULL;
  }
}