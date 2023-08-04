#include "thread.h"
#include "stdint.h"
#include "string.h"
#include "global.h"
#include "debug.h"
#include "interrupt.h"
#include "print.h"
#include "memory.h"

#define PG_SIZE 4096

struct task_struct *main_thread;      //主线程PCB
struct list thread_ready_list;        //就绪队列
struct list thread_all_list;          //所有任务队列
static struct list_elem *thread_tag;  //用于保存队列中的线程节点

extern void switch_to(struct task_struct *cur, struct task_struct *next);

//获取当前线程pcb指针
struct task_struct *running_thread() {
  uint32_t esp;
  asm ("mov %%esp, %0" : "=g"(esp));
  //取esp整数部分，即pcb起始地址
  return (struct task_struct *)(esp & 0xfffff000);
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

//初始化线程栈thread_stack，将待执行的函数和参数放到thread_stack中相应的位置
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

//初始化线程基本信息
//pthread是待初始化线程的指针，name是线程名称，prio是线程的优先级
void init_thread(struct task_struct *pthread, char *name, int prio) {
  memset(pthread, 0, PG_SIZE);  //所在的PCB清0
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

  ASSERT(!list_empty(&thread_ready_list));  //避免无线程可调度的情况
  thread_tag = NULL;    //thread_tag清空
  //将thread_ready_list队列中的第一个就绪线程弹出，准备将其调度上cpu
  thread_tag = list_pop(&thread_ready_list);  //从就绪队列中弹出一个可用线程并存入thread_tag
  struct task_struct *next = elem2entry(struct task_struct, general_tag, thread_tag);
  next->status = TASK_RUNNING;
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

void thread_init(void) {
  put_str("thread_init start\n");
  list_init(&thread_ready_list);
  list_init(&thread_all_list);
  //将当前main函数创建为线程
  make_main_thread();
  put_str("thread_init done\n");
}