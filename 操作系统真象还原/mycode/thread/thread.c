#include "thread.h"
#include "stdint.h"
#include "string.h"
#include "global.h"
#include "debug.h"
#include "interrupt.h"
#include "print.h"
#include "memory.h"
#include "process.h"
#include "sync.h"
#include "fs.h"
#include "file.h"
#include "stdio.h"

#define PG_SIZE 4096

//pid的位图，最大支持1024个pid
uint8_t pid_bitmap_bits[128] = {0};

//pid池，由位图去管理pid的分配与释放
struct pid_pool {
  struct bitmap pid_bitmap;   //pid位图
  uint32_t pid_start;         //起始pid
  struct lock pid_lock;       //分配pid锁
}pid_pool;

struct task_struct *idle_thread;      //idle线程
struct task_struct *main_thread;      //主线程PCB
struct list thread_ready_list;        //就绪队列
struct list thread_all_list;          //所有任务队列
static struct list_elem *thread_tag;  //用于保存队列中的线程节点

extern void switch_to(struct task_struct *cur, struct task_struct *next);
extern void init(void);

//获取当前线程pcb指针
struct task_struct *running_thread() {
  uint32_t esp;
  asm ("mov %%esp, %0" : "=g"(esp));
  //取esp整数部分，即pcb起始地址
  return (struct task_struct *)(esp & 0xfffff000);
}

//系统空闲时运行的线程
static void idle(void* arg UNUSED) {
  while (1) {
    thread_block(TASK_BLOCKED);
    //执行hlt时必须要保证目前处在开中断的情况下
    //执行完处理器已经停止运行，因此并不会再产生内部异常，
    //唯一能唤醒处理器的就是外部中断，当外部发生后，处理器恢复执行后面的指令
    asm volatile("sti; hlt" : : : "memory");
  }
}

//由kernel_thread去执行function(func_arg)
//kernel_thread并不是通过call指令调用的，而是通过ret来执行的
static void kernel_thread(thread_func *function, void *func_arg) {
  //执行function前要开中断，避免后面的时钟中断被屏蔽，而无法调度其他线程
  intr_enable();
  //线程的首次运行是由时钟中断处理函数调用任务调度器schedule完成的，进入中断后处理器会自动关中断，
  //因此在执行function前要打开中断，否则kernel_thread中的function在关中断的情况下运行，
  //也就是时钟中断被屏蔽了，再也不会调度到新的线程，function会独享处理器
  function(func_arg);
}

//初始化pid池
static void pid_pool_init(void) {
  pid_pool.pid_start = 1;
  pid_pool.pid_bitmap.bits = pid_bitmap_bits;
  pid_pool.pid_bitmap.btmp_bytes_len = 128;
  bitmap_init(&pid_pool.pid_bitmap);
  lock_init(&pid_pool.pid_lock);
}

//分配pid
static pid_t allocate_pid(void) {
  lock_acquire(&pid_pool.pid_lock);
  int32_t bit_idx = bitmap_scan(&pid_pool.pid_bitmap, 1);
  bitmap_set(&pid_pool.pid_bitmap, bit_idx, 1);
  lock_release(&pid_pool.pid_lock);
  return (bit_idx + pid_pool.pid_start);
}

//释放pid
void release_pid(pid_t pid) {
  lock_acquire(&pid_pool.pid_lock);
  int32_t bit_idx = pid - pid_pool.pid_start;
  bitmap_set(&pid_pool.pid_bitmap, bit_idx, 0);
  lock_release(&pid_pool.pid_lock);
}

//fork进程时为其分配pid,因为allocate_pid已经是静态的,
//别的文件无法调用.不想改变函数定义了,故定义fork_pid函数来封装一下
pid_t fork_pid(void) {
  return allocate_pid();
}

//初始化线程栈thread_stack，将待执行的函数和参数放到thread_stack中相应的位置  创建线程运行的栈
//pthread是待创建的线程的指针，function是在线程中运行的函数，func_arg是function的参数。
void thread_create(struct task_struct *pthread, thread_func function, void *func_arg) {
  //先预留中断使用栈的空间
  //将来线程进入中断后，位于kernel.S中的中断代码会通过此栈来保存上下文。
  pthread->self_kstack -= sizeof(struct intr_stack);

  //再留出线程栈空间
  //将来实现用户进程时，会将用户进程的初始信息放在中断栈中。
  pthread->self_kstack -= sizeof(struct thread_stack);
  struct thread_stack *kthread_stack = (struct thread_stack*)pthread->self_kstack;
  kthread_stack->eip = kernel_thread;
  kthread_stack->function = function;
  kthread_stack->func_arg = func_arg;
  //4个寄存器初始化为0，因为线程中的函数尚未执行，在执行过程中寄存器才会有值，此时置为0即可。
  kthread_stack->ebp = kthread_stack->ebx = kthread_stack->edi = kthread_stack->esi = 0;
  //kthread_stack->unused_retaddr 是不需要赋值的，就是用来占位的，因此并没有对它处理。
}

//初始化线程基本信息(pcb中的信息)
//pthread是待初始化线程的指针，name是线程名称，prio是线程的优先级
void init_thread(struct task_struct *pthread, char *name, int prio) {
  memset(pthread, 0, PG_SIZE);  //所在的PCB清0
  pthread->pid = allocate_pid();  //为线程分配pid
  strcpy(pthread->name, name);  //将线程名写入PCB中的name数组中

  if (pthread == main_thread) {
    //由于把main函数也封装成一个线程，并且它一直是运行的，故将其直接设为TASK_RUNNING
    pthread->status = TASK_RUNNING;
  } else {
    pthread->status = TASK_READY;
  }

  //self_kstack是线程自己在内核态下使用的栈顶地址
  pthread->self_kstack = (uint32_t *)((uint32_t)pthread + PG_SIZE);
  pthread->priority = prio;     //设置优先级
  pthread->ticks = prio;        //设置时间片
  pthread->elapsed_ticks = 0;   //设置总运行时间为0
  pthread->pgdir = NULL;        //设置页表的虚拟地址
  //预留标准输入输出
  pthread->fd_table[0] = 0;
  pthread->fd_table[1] = 1;
  pthread->fd_table[2] = 2;
  //其余的全置为-1
  uint8_t fd_idx = 3;
  while (fd_idx < MAX_FILES_OPEN_PER_PROC) {
    pthread->fd_table[fd_idx] = -1;
    fd_idx++;
  }

  pthread->cwd_inode_nr = 0;          //以根目录作为默认工作路径
  pthread->parent_pid = -1;           //使任务的父进程默认为−1，−1表示没有父进程
  pthread->stack_magic = 0x19870916;  //自定义的魔数
}

//创建一优先级为prio，名字为name，线程所执行的函数是function(func_arg)
//4个参数，name为线程名，prio为线程的优先级，要执行的函数是function，func_arg是函数function的参数。
struct task_struct *thread_start(char *name, int prio, thread_func function, void *func_arg) {
  //pcb都位于内核空间，包括用户进程的pcb也是在内核空间
  struct task_struct *thread = get_kernel_pages(1); //在内核空间中申请一页内存，get_kernel_page返回的是页的起始地址，故thread指向的是PCB的最低地址。

  init_thread(thread, name, prio);  //初始化刚刚创建的thread线程
  thread_create(thread, function, func_arg);

  //确保之前不在队列中
  ASSERT(!elem_find(&thread_ready_list, &thread->general_tag));
  //加入就绪队列
  list_append(&thread_ready_list, &thread->general_tag);

  //确保之前不在队列中
  ASSERT(!elem_find(&thread_all_list, &thread->all_list_tag));
  //加入全部线程队列
  list_append(&thread_all_list, &thread->all_list_tag);

  return thread;
}

//将kernel中的main函数完善为主线程
static void make_main_thread(void) {
  //因为main线程早已运行，咱们在loader.S中进入内核时的mov esp,0xc009f000，就是为其预留pcb的，
  //因此pcb地址为0xc009e000，不需要通过get_kernel_page另分配一页
  main_thread = running_thread();
  init_thread(main_thread, "main", 31);

  //main函数是当前线程，当前线程不在thread_ready_list中，所以只将其加在thread_all_list中
  ASSERT(!elem_find(&thread_all_list, &main_thread->all_list_tag));
  list_append(&thread_all_list, &main_thread->all_list_tag);
}

//实现任务调度
void schedule() {
  ASSERT(intr_get_status() == INTR_OFF);

  struct task_struct *cur = running_thread(); //获取当前运行线程的PCB
  if (cur->status == TASK_RUNNING) {
    //若此线程只是cpu时间片到了，将其加入到就绪队列尾
    ASSERT(!elem_find(&thread_ready_list, &cur->general_tag));
    list_append(&thread_ready_list, &cur->general_tag);
    //重新将当前线程的ticks再重置为其priority
    cur->ticks = cur->priority;
    cur->status = TASK_READY;
  } else {
    //若此线程需要某事件发生后才能继续上cpu运行，不需要将其加入队列，因为当前线程不在就绪队列中
  }

  //idle_thread在第一次创建时会被加入到就绪队列，因此会执行一次，然后阻塞
  //如果就绪队列中没有可运行的任务，就唤醒idle
  if (list_empty(&thread_ready_list)) {
    thread_unblock(idle_thread);
  }

  ASSERT(!list_empty(&thread_ready_list));  //避免无线程可调度的情况
  thread_tag = NULL;    //thread_tag清空
  //将thread_ready_list队列中的第一个就绪线程弹出，准备将其调度上cpu
  thread_tag = list_pop(&thread_ready_list);  //从就绪队列中弹出一个可用线程并存入thread_tag
  struct task_struct *next = elem2entry(struct task_struct, general_tag, thread_tag);
  next->status = TASK_RUNNING;
  //激活进程的页表等
  process_activate(next);
  switch_to(cur, next);
}

//当前线程将自己阻塞，标志其状态为stat
//接受一个参数stat，stat是线程的状态，它的取值为“不可运行态”，
//函数功能是将当前线程的状态置为stat，从而实现了线程的阻塞
void thread_block(enum task_status stat) {
  //stat取值为TASK_BLOCKED、TASK_WAITING、TASK_HANGING，也就是只有这三种状态才不会被调度
  ASSERT((stat == TASK_BLOCKED) || (stat == TASK_WAITING) || (stat == TASK_HANGING));
  enum intr_status old_status = intr_disable();
  struct task_struct *cur_thread = running_thread();
  cur_thread->status = stat;  //置其状态为stat
  schedule();                 //将当前线程换下处理器
  //待当前线程被解除阻塞后才继续运行下面的intr_set_status
  intr_set_status(old_status);
}

//将线程pthread解除阻塞
void thread_unblock(struct task_struct *pthread) {
  enum intr_status old_status = intr_disable();
  ASSERT((pthread->status == TASK_BLOCKED) || (pthread->status == TASK_WAITING) || (pthread->status == TASK_HANGING));
  if (pthread->status != TASK_READY) {  //某线程被阻塞，肯定不是就绪态
    ASSERT(!elem_find(&thread_ready_list, &pthread->general_tag));  //防止就绪队列中会出现已阻塞的线程
    if (elem_find(&thread_ready_list, &pthread->general_tag)) {
      PANIC("thread_unblock:blocked thread in ready_list\n");
    }
    list_push(&thread_ready_list, &pthread->general_tag); //放到队列的最前面，使其尽快得到调度
    pthread->status = TASK_READY; //将线程改为就绪态
  }
  intr_set_status(old_status);
}

//主动让出cpu，换其他线程运行
//（1）先将当前任务重新加入到就绪队列（队尾）
//（2）然后将当前任务的status置为TASK_READY
//（3）最后调用schedule重新调度新任务
//前2步必须是原子操作
void thread_yield(void) {
  struct task_struct *cur = running_thread();
  enum intr_status old_status = intr_disable();
  ASSERT(!elem_find(&thread_ready_list, &cur->general_tag));
  list_append(&thread_ready_list, &cur->general_tag);
  cur->status = TASK_READY;
  schedule();
  intr_set_status(old_status);
}

//以填充空格的方式输出buf
//用于对齐输出
static void pad_print(char *buf, int32_t buf_len, void *ptr, char format) {
  memset(buf, 0, buf_len);
  uint8_t out_pad_0idx = 0;
  switch(format) {
    case 's':   //处理字符串
      out_pad_0idx = sprintf(buf, "%s", ptr);
      break;
    case 'd':   //处理16位整数
      out_pad_0idx = sprintf(buf, "%d", *((int16_t *)ptr));
      break;
    case 'x':   //处理32位整数
      out_pad_0idx = sprintf(buf, "%x", *((uint32_t *)ptr));
  }
  while (out_pad_0idx < buf_len) {    //以空格填充
    buf[out_pad_0idx] = ' ';
    out_pad_0idx++;
  }
  sys_write(stdout_no, buf, buf_len - 1);
}

//用于在list_traversal函数中的回调函数，用于针对线程队列的处理，打印任务信息
static bool elem2thread_info(struct list_elem *pelem, int arg UNUSED) {
  struct task_struct *pthread = elem2entry(struct task_struct, all_list_tag, pelem);
  char out_pad[16] = {0};

  pad_print(out_pad, 16, &pthread->pid, 'd');

  if (pthread->parent_pid == -1) {
    pad_print(out_pad, 16, "NULL", 's');
  } else {
    pad_print(out_pad, 16, &pthread->parent_pid, 'd');
  }
  switch (pthread->status) {
    case 0:
      pad_print(out_pad, 16, "RUNNING", 's');
      break;
    case 1:
      pad_print(out_pad, 16, "READY", 's');
      break;
    case 2:
      pad_print(out_pad, 16, "BLOCKED", 's');
      break;
    case 3:
      pad_print(out_pad, 16, "WAITING", 's');
      break;
    case 4:
      pad_print(out_pad, 16, "HANGING", 's');
      break;
    case 5:
      pad_print(out_pad, 16, "DIED", 's');
  }
  pad_print(out_pad, 16, &pthread->elapsed_ticks, 'x');

  memset(out_pad, 0, 16);
  ASSERT(strlen(pthread->name) < 17);
  memcpy(out_pad, pthread->name, strlen(pthread->name));
  strcat(out_pad, "\n");
  sys_write(stdout_no, out_pad, strlen(out_pad));
  return false;     //此处返回false是为了迎合主调函数list_traversal，只有回调函数返回false时才会继续调用此函数
}

//打印任务列表
void sys_ps(void) {
  char* ps_title = "PID            PPID           STAT           TICKS          COMMAND\n";
  sys_write(stdout_no, ps_title, strlen(ps_title));
  list_traversal(&thread_all_list, elem2thread_info, 0);
}

//回收thread_over的pcb和页表，并将其从调度队列中去除
//接受2个参数，待退出的任务thread_over、是否要调度标记need_schedule，
//功能是回收thread_over的pcb和页表，并将其从调度队列中去除
void thread_exit(struct task_struct *thread_over, bool need_schedule) {
  //要保证schedule在关中断情况下调用
  enum intr_status old_status= intr_disable();
  thread_over->status = TASK_DIED;

  //如果thread_over不是当前线程，就有可能还在就绪队列中，将其从中删除
  if (elem_find(&thread_ready_list, &thread_over->general_tag)) {
    list_remove(&thread_over->general_tag);
  }
  if (thread_over->pgdir) {     //如是进程，回收进程的页表
    mfree_page(PF_KERNEL, thread_over->pgdir, 1);
  }

  //从all_thread_list中去掉此任务
  list_remove(&thread_over->all_list_tag);

  //回收pcb所在的页，主线程的pcb不在堆中，跨过
  if (thread_over != main_thread) {
    mfree_page(PF_KERNEL, thread_over, 1);
  }

  //归还pid
  release_pid(thread_over->pid);

  //如果需要下一轮调度则主动调用schedule
  if (need_schedule) {
    schedule();
    PANIC("thread_exit: should not be here\n");
  }

  //感觉此处需要恢复中断
  intr_set_status(old_status);    //自己加的
}

//比对任务的pid
//是函数listr_traversal的回调函数，它用于比对任务的pid，找到特定pid的任务就返回。
static bool pid_check(struct list_elem* pelem, int32_t pid) {
  struct task_struct* pthread = elem2entry(struct task_struct, all_list_tag, pelem);
  if (pthread->pid == pid) {
    return true;
  }
  return false;
}

//根据pid找pcb,若找到则返回该pcb,否则返回NULL
struct task_struct* pid2thread(int32_t pid) {
  struct list_elem* pelem = list_traversal(&thread_all_list, pid_check, pid);
  if (pelem == NULL) {
    return NULL;
  }
  struct task_struct* thread = elem2entry(struct task_struct, all_list_tag, pelem);
  return thread;
}

//初始化线程环境
void thread_init(void) {
  put_str("thread_init start\n");
  list_init(&thread_ready_list);
  list_init(&thread_all_list);
  pid_pool_init();

  //先创建第一个用户进程:init
  process_execute(init, "init");    //放在第一个初始化，这是第一个进程，init进程的pid为1

  //将当前main函数创建为线程
  make_main_thread();
  //创建idle线程
  idle_thread = thread_start("idle", 1, idle, NULL);
  put_str("thread_init done\n");
}