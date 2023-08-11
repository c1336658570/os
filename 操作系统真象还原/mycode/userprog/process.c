#include "process.h"
#include "global.h"
#include "debug.h"
#include "memory.h"
#include "thread.h"    
#include "list.h"    
#include "tss.h"    
#include "interrupt.h"
#include "string.h"
#include "console.h"

extern void intr_exit(void);  //声明了外部函数intr_exit，用户进程通过该函数进入，在kernel.S中定义

//构建用户进程初始上下文信息
//接收一个参数filename_，此参数表示用户程序的名称。此函数用来创建用户进程filename_的上下文，
//也就是填充用户进程的struct intr_stack，通过假装从中断返回的方式，间接使filename_运行。
void start_process(void *filename_) {
  void *function = filename_;
  struct task_struct *cur = running_thread();
  //用户进程上下文保存在struct intr_stack栈中。在函数thread_create中执行了
  //“pthread->self_kstack -= sizeof(struct intr_stack);”
  //和“pthread->self_kstack -= sizeof(struct thread_stack);”
  //所以需要使指针self_kstack跨过struct thread_stack最终指向struct intr_stack栈的最低处
  cur->self_kstack += sizeof(struct thread_stack);
  struct intr_stack *proc_stack = (struct intr_stack *)cur->self_kstack;
  proc_stack->edi = proc_stack->esi = proc_stack->ebp = proc_stack->esp_dummy = 0;
  proc_stack->ebx = proc_stack->edx = proc_stack->ecx = proc_stack->eax = 0;
  proc_stack->gs = 0;   //用户态用不上，直接初始为0,操作系统不允许用户进程访问显存
  proc_stack->ds = proc_stack->es = proc_stack->fs = SELECTOR_U_DATA;
  proc_stack->eip = function;   //待执行的用户程序地址
  proc_stack->cs = SELECTOR_U_CODE; //将栈中代码段寄存器cs赋值为先前我们已在GDT中安装好的用户级代码段
  proc_stack->eflags = (EFLAGS_IOPL_0 | EFLAGS_MBS | EFLAGS_IF_1);  //对栈中eflags赋值
  //为用户进程分配3特权级下的栈
  /*
  此处为用户进程创建的 3 特权级栈，它是在谁的内存空间中申请的？换句话说，
  是安装在谁的页表中了？不会是安装到内核线程使用的页表中了吧？当然不是，用户进程使用的 3 级栈
  必然要建立在用户进程自己的页表中。您可以回顾一下图 11-12，有两项工作确保了这件事的正确性，
  在进程创建部分，有一项工作是 create_page_dir，这是提前为用户进入创建了页目录表，在进程执行部
  分，有一项工作是 process_activate，这是使任务（无论任务是否为新创建的进程或线程，或是老进程、老
  线程）自己的页表生效。我们是在函数 start_process 中为用户进程创建了 3 特权级栈，start_process 是在
  执行任务页表激活之后执行的，也就是在 process_activate）之后运行，那时已经把页表更新为用户进程的
  页表了，所以 3 特权级栈是安装在用户进程自己的页表中的。
  */
  proc_stack->esp = (void *)((uint32_t)get_a_page(PF_USER, USER_STACK3_VADDR) + PG_SIZE);
  proc_stack->ss = SELECTOR_U_DATA; //栈中SS赋值为用户数据段选择子SELECTOR_U_DATA
  //将栈esp替换为我们刚刚填充好的proc_stack，通过jmp intr_exit使程序流程跳转到中断出口地址intr_exit
  asm volatile ("movl %0, %%esp; jmp intr_exit" : : "g" (proc_stack) : "memory");
}

//激活页表
/*
目前咱们的线程并不是为用户进程服务的，它是为内核服务的，
因此与内核共享同一地址空间，也就是和内核用的是同一套页表。当进程 A 切换到进程 B 时，页表也要
随之切换到进程 B 所用的页表，这样才保证了地址空间的独立性。当进程 B 又切换到线程 C 时，由于目
前在页表寄存器 CR3 中的还是进程 B 的页表，因此，必须要将页表更换为内核所使用的页表。
*/
void page_dir_activate(struct task_struct *p_thread) {
  //执行此函数时,当前任务可能是线程。
  //之所以对线程也要重新安装页表, 原因是上一次被调度的可能是进程,
  //否则不恢复页表的话,线程就会使用进程的页表了。

  //若为内核线程,需要重新填充页表为0x100000
  uint32_t pagedir_phy_addr = 0x100000;

  //默认为内核的页目录物理地址，也就是内核线程所用的页目录表
  //判断pcb中的pgdir是否等于NULL，来判断是内核线程还是用户进程
  if (p_thread->pgdir != NULL) {    //用户态进程有自己的页目录表
    //pgdir中的是页表的虚拟地址，要将其转换成物理地址
    pagedir_phy_addr = addr_v2p((uint32_t)p_thread->pgdir);
  }

  //更新页目录寄存器cr3，使新页表生效
  asm volatile("movl %0, %%cr3" : : "r"(pagedir_phy_addr) : "memory");
}

//激活线程或进程的页表，更新tss中的esp0为进程的特权级0的栈
void process_activate(struct task_struct *p_thread) {
  ASSERT(p_thread != NULL);
  //激活该进程或线程的页表
  page_dir_activate(p_thread);
  //内核线程特权级本身就是0，处理器进入中断时并不会从tss中获取0特权级栈地址，故不需要更新esp0
  //如果是用户进程的话才去更新tss中的esp0
  if (p_thread->pgdir) {
    //更新该进程的esp0，用于此进程被中断时保留上下文
    update_tss_esp(p_thread);
  }
}

//创建页目录表，将当前页表的表示内核空间的pde复制，成功则返回页目录的虚拟地址，否则返回-1
uint32_t *create_page_dir(void) {
  //用户进程的页表不能让用户直接访问到，所以在内核空间来申请
  uint32_t *page_dir_vaddr = get_kernel_pages(1);
  if (page_dir_vaddr == NULL) {
    console_put_str("create_page_dir: get_kernel_page failed!\n");
    return NULL;
  }
  //********************* 1 先复制页表 *************************
  //page_dir_vaddr + 0x300*4是内核页目录的第768项
  //把用户进程页目录表中的第768～1023个页目录项
  //用内核页目录表的第768～1023个页目录项代替，其实就是将
  //内核所在的页目录项复制到进程页目录表中同等位置，这样就
  //能让用户进程的高1GB空间指向内核。
  memcpy((uint32_t *)((uint32_t)page_dir_vaddr + 0x300*4), (uint32_t *)(0xfffff000+0x300*4), 1024);
  
  //********************* 2 更新页目录地址 *********************
  uint32_t new_page_dir_phy_addr = addr_v2p((uint32_t)page_dir_vaddr);
  //页目录地址是存入在页目录的最后一项，更新页目录地址为新页目录的物理地址
  //把用户页目录表中最后一个页目录项更新为用户进程自己的页目录表的物理地址
  //这么做的原因是将来用户进程运行时，执行期间有可能会有页表操作，页表操作是由内核代码完成的，
  //因此内核需要知道该用户进程的页目录表在哪里。
  page_dir_vaddr[1023] = new_page_dir_phy_addr | PG_US_U | PG_RW_W | PG_P_1;

  return page_dir_vaddr;
}

//创建用户进程虚拟地址位图
void create_user_vaddr_bitmap(struct task_struct *user_prog) {
  //我们为用户进程定的起始地址是USER_VADDR_START，该值定义在process.h中，
  //其值为0x8048000，这是Linux用户程序入口地址
  user_prog->userprog_vaddr.vaddr_start = USER_VADDR_START;
  //求位图占据多少个内存页
  uint32_t bitmap_pg_cnt = DIV_ROUND_UP((0xc0000000 - USER_VADDR_START) / PG_SIZE / 8, PG_SIZE);  //向上取整的除法
  user_prog->userprog_vaddr.vaddr_bitmap.bits = get_kernel_pages(bitmap_pg_cnt);  //为位图分配内存
  //记录位图的长度到btmp_bytes_len
  user_prog->userprog_vaddr.vaddr_bitmap.btmp_bytes_len = (0xc0000000 - USER_VADDR_START) / PG_SIZE / 8;
  //初始化位图
  bitmap_init(&user_prog->userprog_vaddr.vaddr_bitmap);
}

//创建用户进程
void process_execute(void *filename, char *name) {
  //pcb内核的数据结构，由内核来维护进程信息，因此要在内核内存池中申请
  struct task_struct *thread = get_kernel_pages(1);
  init_thread(thread, name, default_prio);
  create_user_vaddr_bitmap(thread);
  thread_create(thread, start_process, filename);
  thread->pgdir = create_page_dir();
  block_desc_init(thread->u_block_desc);

  enum intr_status old_status = intr_disable();
  ASSERT(!elem_find(&thread_ready_list, &thread->general_tag));
  list_append(&thread_ready_list, &thread->general_tag);

  ASSERT(!elem_find(&thread_all_list, &thread->all_list_tag));
  list_append(&thread_all_list, &thread->all_list_tag);
  intr_set_status(old_status);
}