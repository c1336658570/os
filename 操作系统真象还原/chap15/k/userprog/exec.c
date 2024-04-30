#include "exec.h"
#include "thread.h"    
#include "stdio-kernel.h"
#include "fs.h"
#include "string.h"
#include "global.h"
#include "memory.h"

extern void intr_exit(void);
typedef uint32_t Elf32_Word, Elf32_Addr, Elf32_Off;
typedef uint16_t Elf32_Half;

//32位elf头
struct Elf32_Ehdr {
  unsigned char e_ident[16];    //elf格式的魔数，e_ident[7～15]暂时未用
  Elf32_Half    e_type;
  Elf32_Half    e_machine;
  Elf32_Word    e_version;
  Elf32_Addr    e_entry;
  Elf32_Off     e_phoff;
  Elf32_Off     e_shoff;
  Elf32_Word    e_flags;
  Elf32_Half    e_ehsize;
  Elf32_Half    e_phentsize;
  Elf32_Half    e_phnum;
  Elf32_Half    e_shentsize;
  Elf32_Half    e_shnum;
  Elf32_Half    e_shstrndx;
};

//程序头表Program header.就是段描述头，也就是段头表
struct Elf32_Phdr {
  Elf32_Word p_type;    //见下面的enum segment_type
  Elf32_Off  p_offset;
  Elf32_Addr p_vaddr;
  Elf32_Addr p_paddr;
  Elf32_Word p_filesz;
  Elf32_Word p_memsz;
  Elf32_Word p_flags;
  Elf32_Word p_align;
};

//段类型，只关注类型为PT_LOAD的段
enum segment_type {
  PT_NULL,        //忽略
  PT_LOAD,        //可加载程序段
  PT_DYNAMTC,     //动态加载信息
  PT_INTERP,      //动态加载器名称
  PT_NOTE,        //一些辅助信息
  PT_SHLIB,       //保留
  PT_PHDR         //程序头表
};

//文件描述符fd指向的文件中，偏移为offset，大小为filesz的段加载到虚拟地址为vaddr的内存
//接受4个参数，文件描述符fd、段在文件中的字节偏移量offset、段大小filesz、段被加载到的虚拟地址vaddr，
//函数功能是将文件描述符fd指向的文件中，偏移为offset，大小为filesz的段加载到虚拟地址为vaddr的内存空间
static bool segment_load(int32_t fd, uint32_t offset, uint32_t filesz, uint32_t vaddr) {
  uint32_t vaddr_first_page = vaddr & 0xfffff000;     //vaddr地址所在的页框
  uint32_t size_in_first_page = PG_SIZE - (vaddr & 0x00000fff); //加载到内存后，文件在第一个页框中占用的字节大小
  uint32_t occupy_pages = 0;
  //若一个页框容不下该段
  if (filesz > size_in_first_page) {
    uint32_t left_size = filesz - size_in_first_page;
    occupy_pages = DIV_ROUND_UP(left_size, PG_SIZE) + 1;  //1是指vaddr_first_page
  } else {
    occupy_pages = 1;
  }

  //为进程分配内存
  uint32_t page_idx = 0;
  uint32_t vaddr_page = vaddr_first_page;
  while (page_idx < occupy_pages) {
    uint32_t *pde = pde_ptr(vaddr_page);
    uint32_t *pte = pte_ptr(vaddr_page);
    //如果pde不存在，或者pte不存在就分配内存．pde的判断要在pte之前，否则pde若不存在会导致判断pte时缺页异常
    if (!(*pde & 0x00000001) || !(*pte & 0x00000001)) {
      if (get_a_page(PF_USER, vaddr_page) == NULL) {
        //exec是执行新进程，也就是用新进程的进程体替换当前老进程，但依然用的是老进程的那套页表，
        //这就涉及到老进程的页表是否满足新进程内存要求了。如果老进程已经分配了页框，我们不需要再重新分配页框，
        //只需要用新进程的进程体覆盖老进程就行了，只有新进程用到了在老进程中没有的地址时才需要分配新页框给新进程
        return false;
      }
    } //如果原进程的页表已经分配了，利用现有的物理页
      //直接覆盖进程体
      vaddr_page += PG_SIZE;
      page_idx++;
  }
  sys_lseek(fd, offset, SEEK_SET);
  sys_read(fd, (void *)vaddr, filesz);
  return true;
}

//从文件系统上加载用户程序pathname，成功则返回程序的起始地址，否则返回−1
//接受1个参数，可执行文件的绝对路径pathname，功能是从文件系统上加载用户程序pathname，成功则返回程序的起始地址，否则返回−1。
static int32_t load(const char *pathname) {
  struct task_struct *cur = running_thread();

  int32_t ret = -1;
  struct Elf32_Ehdr elf_header;   //elf头
  struct Elf32_Phdr prog_header;  //程序头
  memset(&elf_header, 0, sizeof(struct Elf32_Ehdr));

  int32_t fd = sys_open(pathname, O_RDONLY);
  if (fd == -1) {
    return -1;
  }

  if (sys_read(fd, &elf_header, sizeof(struct Elf32_Ehdr)) != sizeof(struct Elf32_Ehdr)) {
    ret = -1;
    goto done;
  }

  //校验elf头，判断加载的文件是否是elf格式的
  //开头的4个字节是固定不变的，它们分别是0x7f和字符串“ELF”的asc码0x45、0x4c和0x46。
  //成员e_ident[4]表示elf是32位，还是64位，值为1表示32位，值为2表示64位。e_ident[5]表示字节序，
  //值为1表示小端字节序，值为2表示大端字节序。e_ident[6]表示elf版本信息，默认为1
  if (memcmp(elf_header.e_ident, "\177ELF\1\1\1", 7) \
      || elf_header.e_type != 2 \
      || elf_header.e_machine != 3 \
      || elf_header.e_version != 1 \
      || elf_header.e_phnum > 1024 \
      || elf_header.e_phentsize != sizeof(struct Elf32_Phdr)) {
    //elf格式的魔数，e_ident[7～15]暂时未用
    //e_type表示目标文件类型，其值应该为ET_EXEC，即等于2
    //e_machine表示体系结构，其值应该为EM_386，即等于3
    //e_version表示版本信息
    //e_phnum用来指明程序头表中条目的数量，也就是段的个数
    ret = -1;
    goto done;
  }

  Elf32_Off prog_header_offset = elf_header.e_phoff;      //程序头的起始地址记录在
  Elf32_Half prog_header_size = elf_header.e_phentsize;   //程序头条目大小

  //遍历所有程序头
  uint32_t prog_idx = 0;
  
  while (prog_idx < elf_header.e_phnum) {   //段的数量在e_phnum中记录
    memset(&prog_header, 0, prog_header_size);

    //将文件的指针定位到程序头
    sys_lseek(fd, prog_header_offset, SEEK_SET);

    //只获取程序头
    if (sys_read(fd, &prog_header, prog_header_size) != prog_header_size) {
      ret = -1;
      goto done;
    }

    //如果是可加载段就调用segment_load加载到内存
    if (PT_LOAD == prog_header.p_type) {
      //block_desc_init(cur->u_block_desc);
      if (!segment_load(fd, prog_header.p_offset, prog_header.p_filesz, prog_header.p_vaddr)) {
        ret = -1;
        goto done;
      }
      block_desc_init(cur->u_block_desc);
    }
    //更新下一个程序头的偏移
    prog_header_offset += elf_header.e_phentsize;
    prog_idx++;
  }
  ret = elf_header.e_entry;

done:
  sys_close(fd);
  return ret;
}

//用path指向的程序替换当前进程
//接受2个参数，path是可执行文件的绝对路径，数组argv是传给可执行文件的参数，
//函数功能是用path指向的程序替换当前进程。函数失败则返回−1，如果成功则没机会返回
int32_t sys_execv(const char *path, const char *argv[]) {
  uint32_t argc = 0;
  while (argv[argc]) {  //统计出参数个数，存放到变量argc中
    argc++;
  }
  
  struct task_struct *cur = running_thread();
  block_desc_init(cur->u_block_desc);

  int32_t entry_point = load(path);
  if (entry_point == -1) {    //若加载失败，则返回−1
    return -1;
  }

  //修改进程名
  memcpy(cur->name, path, TASK_NAME_LEN);
  cur->name[TASK_NAME_LEN - 1] = 0;

  //修改内核栈中的参数
  struct intr_stack *intr_0_stack = (struct intr_stack *)((uint32_t)cur + PG_SIZE - sizeof(struct intr_stack));
  //参数传递给用户进程
  intr_0_stack->ebx = (int32_t)argv;    //将参数数组argv的地址赋值给栈中ebx寄存器
  intr_0_stack->ecx = argc;             //参数个数argc赋值给栈中ecx寄存器
  intr_0_stack->eip = (void *)entry_point;
  //使新用户进程的栈地址为最高用户空间地址
  //然后将内核
  //栈中的用户栈指针esp恢复为0xc0000000，这样做的原因有两个，一是老进程用户栈中的数据只适用于老
  //进程，对新进程没用，故新进程的用户栈应该从新开始。二是为了后续传入参数做准备，在很久以前就说
  //过了，用户空间的最高处用于存储命令行参数，以后实现传参时就清楚了。
  intr_0_stack->esp = (void *)0xc0000000;
  
  //exec不同于fork，为使新进程更快被执行，直接从中断返回
  asm volatile("mov %0, %%esp; jmp intr_exit" : : "g"(intr_0_stack) : "memory");

  return 0;   //为了满足编译器gcc的c语法，即“make gcc happy”。
}