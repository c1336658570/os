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
static char *path_parse(char *pathname, char *name_store) {
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
static uint32_t fd_local2global(uint32_t local_fd) {
  struct task_struct *cur = running_thread();
  int32_t global_fd = cur->fd_table[local_fd];
  ASSERT(global_fd >= 0 && global_fd < MAX_FILE_OPEN);
  return (uint32_t)global_fd;
}

//关闭文件描述符fd指向的文件，成功返回 0，否则返回-1
//接受1个参数，文件描述符fd，功能是关闭文件描述符fd指向的文件，成功返回0，否则返回−1。
int32_t sys_close(int32_t fd) {
  int32_t ret = -1;
  if (fd > 2) {
    uint32_t _fd = fd_local2global(fd);
    ret = file_close(&file_table[_fd]);
    running_thread()->fd_table[fd] = -1;    //使该文件描述符位可用
  }
  return ret;
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