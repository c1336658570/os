/*
进程有哪些资源呢？fork要复制哪些资源？
（1）进程的pcb，即task_struct，这是让任务有“存在感”的身份证。
（2）程序体，即代码段数据段等，这是进程的实体。
（3）用户栈，不用说了，编译器会把局部变量在栈中创建，并且函数调用也离不了栈。
（4）内核栈，进入内核态时，一方面要用它来保存上下文环境，另一方面的作用同用户栈一样。
（5）虚拟地址池，每个进程拥有独立的内存空间，其虚拟地址是用虚拟地址池来管理的。
（6）页表，让进程拥有独立的内存空间。
克隆出来的进程该如何执行呢？只要将新进程加入到就绪队列中就可以。
*/
#include "fork.h"
#include "process.h"
#include "memory.h"
#include "interrupt.h"
#include "debug.h"
#include "thread.h"    
#include "string.h"
#include "file.h"
#include "pipe.h"

extern void intr_exit(void);

//将父进程的pcb拷贝给子进程
//接受2个参数，子进程child_thread、父进程parent_thread，功能是将父进程的pcb、虚拟地址位图拷贝给子进程。
static int32_t copy_pcb_vaddrbitmap_stack0(struct task_struct *child_thread, struct task_struct *parent_thread) {
  //a 复制pcb所在的整个页，里面包含进程pcb信息及特级0极的栈，里面包含了返回地址
  memcpy(child_thread, parent_thread, PG_SIZE); //把父进程的pcb及其内核栈一同复制给子进程
  //下面再单独修改pcb中的属性值
  child_thread->pid = fork_pid();   //为子进程分配新的pid
  child_thread->elapsed_ticks = 0;
  child_thread->status = TASK_READY;
  child_thread->ticks = child_thread->priority;     //为新进程把时间片充满
  child_thread->parent_pid = parent_thread->pid;
  child_thread->general_tag.prev = child_thread->general_tag.next = NULL;
  child_thread->all_list_tag.prev = child_thread->all_list_tag.next = NULL;
  //初始化进程自己的内存块描述符，如果没这句代码的话，此处继承的是父进程的块描述符，子进程分配内存时会导致缺页异常
  block_desc_init(child_thread->u_block_desc);
  //b 复制父进程的虚拟地址池的位图
  //计算虚拟地址位图需要的页框数bitmap_pg_cnt
  uint32_t bitmap_pg_cnt = DIV_ROUND_UP((0xc0000000 - USER_VADDR_START) / PG_SIZE / 8, PG_SIZE);
  void *vaddr_btmp = get_kernel_pages(bitmap_pg_cnt); //申请bitmap_pg_cnt一个内核页框来存储位图
  //此时child_thread->userprog_vaddr.vaddr_bitmap.bits还是指向父进程虚拟地址的位图地址
  //下面将child_thread->userprog_vaddr.vaddr_bitmap.bits指向自己的位图vaddr_btmp
  memcpy(vaddr_btmp, child_thread->userprog_vaddr.vaddr_bitmap.bits, bitmap_pg_cnt * PG_SIZE);
  child_thread->userprog_vaddr.vaddr_bitmap.bits = vaddr_btmp;
  //调试用，调试过后会把这两行代码删掉
  // ASSERT(strlen(child_thread->name) < 11);    //pcb.name的长度是16，为避免下面strcat越界
  // strcat(child_thread->name, "_fork");
  return 0;
}

//复制子进程的进程体（代码和数据）及用户栈
//接受3个参数，子进程child_thread、父进程parent_thread、页缓冲区buf_page，buf_page必须是内核页，
//我们要用它作为所有进程的数据共享缓冲区。函数功能是复制子进程的进程体及用户栈。
//此函数的主要功能就是拷贝进程的代码和数据资源，也就是复制一份进程体。
static void copy_body_stack3(struct task_struct* child_thread, struct task_struct* parent_thread, void* buf_page) {
  uint8_t *vaddr_btmp = parent_thread->userprog_vaddr.vaddr_bitmap.bits;
  uint32_t btmp_bytes_len = parent_thread->userprog_vaddr.vaddr_bitmap.btmp_bytes_len;
  uint32_t vaddr_start = parent_thread->userprog_vaddr.vaddr_start;
  uint32_t idx_byte = 0;
  uint32_t idx_bit = 0;
  uint32_t prog_vaddr = 0;

  //在父进程的用户空间中查找已有数据的页，将父进程用户空间中的数据复制到子进程的用户空间
  //各用户进程不能互相访问彼此的空间，但高1GB是内核空间，内核空间是所有用户
  //进程共享的，因此要想把数据从一个进程拷贝到另一个进程，必须要借助内核空间作为数据中转，即先将
  //父进程用户空间中的数据复制到内核的buf_page中，然后再将buf_page复制到子进程的用户空间中
  //在父进程虚拟地址空间中每找到一页占用的内存，就在
  //子进程的虚拟地址空间中分配一页内存，然后将buf_page中父进程的数据复制到为子进程新分配的虚拟
  //地址空间页，也就是一页一页的对拷，因此我们的buf_page只要1页大小就够了
  while (idx_byte < btmp_bytes_len) {   //逐字节查看位图
    if (vaddr_btmp[idx_byte]) {   //如果该字节不为0，也就是某位为1，已分配
      idx_bit = 0;
      while (idx_bit < 8) {   //逐位查看该字节
        if ((BITMAP_MASK << idx_bit) & vaddr_btmp[idx_byte]) {    //如果某位的值为1
          prog_vaddr = (idx_byte * 8 + idx_bit) * PG_SIZE + vaddr_start;  //将该位转换为虚拟地址prog_vaddr
          //下面的操作是将父进程用户空间中的数据通过内核空间做中转,最终复制到子进程的用户空间

          //a 将父进程在用户空间中的数据复制到内核缓冲区buf_page,
          //目的是下面切换到子进程的页表后,还能访问到父进程的数据
          memcpy(buf_page, (void*)prog_vaddr, PG_SIZE);   //父进程的数据拷贝到内核空间

          //b 将页表切换到子进程,目的是避免下面申请内存的函数将pte及pde安装在父进程的页表中
          page_dir_activate(child_thread);    //激活子进程的页表
          //c 申请虚拟地址prog_vaddr
          get_a_page_without_opvaddrbitmap(PF_USER, prog_vaddr);    //为子进程分配1页

          //d 从内核缓冲区中将父进程数据复制到子进程的用户空间
          memcpy((void*)prog_vaddr, buf_page, PG_SIZE);   //内核空间到子进程空间的复制

          //e 恢复父进程页表
          page_dir_activate(parent_thread);   //将父进程的页表恢复
        }
        idx_bit++;
      }
    }
    idx_byte++;
  }
}

//为子进程构建thread_stack和修改返回值
//接受1个参数，子进程child_thread。功能是为子进程构建thread_stack和修改返回值。
/*
父进程在执行fork系统调用时会进入内核态，中断入口程序会保存父进程的上下文，这
其中包括进程在用户态下的CS:EIP的值，因此父进程从fork系统调用返回后，可以继续fork之后的代码执
行。问题来了，子进程也是从fork后的代码处继续运行的，这是怎样做到的呢？
之前我们已经通过函数copy_pcb_vaddrbitmap_stack0将父进程的内核栈复制到了子进程的内核
栈中，那里保存了返回地址，也就是fork之后的地址，为了让子进程也能继续fork之后的代码运行，咱
们必须让它同父进程一样，从中断退出，也就是要经过intr_exit。
子进程是由调试器schedule调度执行的，它要用到switch_to函数，
而switch_to函数要从栈thread_stack中恢复上下文，因此我们要想办法构建出合适的thread_stack。
*/
static int32_t build_child_stack(struct task_struct* child_thread) {
  //a 使子进程pid返回值为0
  //获取子进程0级栈栈顶
  struct intr_stack *intr_0_stack = (struct intr_stack*)((uint32_t)child_thread + PG_SIZE - sizeof(struct intr_stack));
  //修改子进程的返回值为0，fork会为子进程返回0值
  intr_0_stack->eax = 0;

  //b 为switch_to构建struct thread_stack,将其构建在紧临intr_stack之下的空间
  uint32_t *ret_addr_in_thread_stack  = (uint32_t *)intr_0_stack - 1;  //此地址是thread_stack栈中eip的位置

  //这三行不是必要的,只是为了梳理thread_stack中的关系，使thread_stack栈显得更加具有可读性，实际运行中不需要它们的具体值
  //分别为thread_stack中的esi、edi、ebx、ebp安排位置
  uint32_t *esi_ptr_in_thread_stack = (uint32_t *)intr_0_stack - 2; 
  uint32_t *edi_ptr_in_thread_stack = (uint32_t *)intr_0_stack - 3; 
  uint32_t *ebx_ptr_in_thread_stack = (uint32_t *)intr_0_stack - 4; 

  //ebp在thread_stack中的地址便是当时的esp(0级栈的栈顶),即esp为"(uint32_t*)intr_0_stack - 5"
  uint32_t *ebp_ptr_in_thread_stack = (uint32_t *)intr_0_stack - 5;   /*它是thread_stack的栈顶，
  我们必须把它的值存放在pcb中偏移为0的地方，即task_struct中的self_kstack处，
  将来switch_to要用它作为栈顶，并且执行一系列的pop来恢复上下文*/

  //switch_to的返回地址更新为intr_exit,直接从中断返回
  *ret_addr_in_thread_stack = (uint32_t)intr_exit;  /*将ret_addr_in_thread_stack处的值赋值为
  intr_exit的地址，保证了子进程被调度时，可以直接从中断返回，也就是实现了从fork之后的代码处继续
  执行的目的*/

  //下面这两行赋值只是为了使构建的thread_stack更加清晰,其实也不需要,
  //因为在进入intr_exit后一系列的pop会把寄存器中的数据覆盖
  *ebp_ptr_in_thread_stack = *ebx_ptr_in_thread_stack =\
  *edi_ptr_in_thread_stack = *esi_ptr_in_thread_stack = 0;

  //把构建的thread_stack的栈顶做为switch_to恢复数据时的栈顶
  child_thread->self_kstack = ebp_ptr_in_thread_stack;  /*把ebp_ptr_in_thread_stack的值，
  也就是thread_stack的栈顶记录在pcb的self_kstack处，
  这样switch_to便获得了咱们刚刚构建的thread_stack栈顶，从而使程序迈向intr_exit*/   
  return 0;
}

//更新inode打开数
//接受1个参数，线程thread，功能是fork之后，更新线程thread的inode打开数
static void update_inode_open_cnts(struct task_struct* thread) {
  int32_t local_fd = 3, global_fd = 0;
  //遍历fd_table中除前3个标准文件描述符之外的所有文件描述符，
  //从中获得全局文件表file_table的下标global_fd，通过它在file_table中找到对应的文件结构，使相应文件
  //结构中fd_inode的i_open_cnts加1。
  while (local_fd < MAX_FILES_OPEN_PER_PROC) {
    global_fd = thread->fd_table[local_fd];
    ASSERT(global_fd < MAX_FILE_OPEN);
    if (global_fd != -1) {
      if (is_pipe(local_fd)) {
        file_table[global_fd].fd_pos++;
      } else {
        file_table[global_fd].fd_inode->i_open_cnts++;
      }
    }
    local_fd++;
  }
}

//拷贝父进程本身所占资源给子进程
//接受2个参数，子进程child_thread和父进程parent_thread，功能是拷贝父进程本身所占资源给子进程。
static int32_t copy_process(struct task_struct* child_thread, struct task_struct* parent_thread) {
  //内核缓冲区,作为父进程用户空间的数据复制到子进程用户空间的中转
  void *buf_page = get_kernel_pages(1); //申请了1页的内核空间作为内核缓冲区
  if (buf_page == NULL) {
    return -1;
  }

  //a 复制父进程的pcb、虚拟地址位图、内核栈到子进程
  if (copy_pcb_vaddrbitmap_stack0(child_thread, parent_thread) == -1) {
    return -1;
  }

  //b 为子进程创建页表,此页表仅包括内核空间
  child_thread->pgdir = create_page_dir();
  if(child_thread->pgdir == NULL) {
    return -1;
  }

  //c 复制父进程进程体及用户栈给子进程
  copy_body_stack3(child_thread, parent_thread, buf_page);

  //d 构建子进程thread_stack和修改返回值pid
  build_child_stack(child_thread);

  //e 更新文件inode的打开数
  update_inode_open_cnts(child_thread);

  mfree_page(PF_KERNEL, buf_page, 1);   //释放buf_page
  return 0;
}

//fork子进程,内核线程不可直接调
pid_t sys_fork(void) {
  struct task_struct *parent_thread = running_thread();
  struct task_struct *child_thread = get_kernel_pages(1);   //为子进程创建pcb(task_struct结构)
  if (child_thread == NULL) {
    return -1;
  }
  ASSERT(INTR_OFF == intr_get_status() && parent_thread->pgdir != NULL);

  if (copy_process(child_thread, parent_thread) == -1) {
    return -1;
  }

  //添加到就绪线程队列和所有线程队列,子进程由调试器安排运行
  ASSERT(!elem_find(&thread_ready_list, &child_thread->general_tag));
  list_append(&thread_ready_list, &child_thread->general_tag);
  ASSERT(!elem_find(&thread_all_list, &child_thread->all_list_tag));
  list_append(&thread_all_list, &child_thread->all_list_tag);

  return child_thread->pid;     //父进程返回子进程的pid
}