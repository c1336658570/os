#include "thread.h"
#include "stdint.h"
#include "string.h"
#include "global.h"
#include "memory.h"

#define PG_SIZE 4096

//由kernel_thread去执行function(func_arg)
//kernel_thread并不是通过call指令调用的，而是通过ret来执行的
static void kernel_thread(thread_func *function, void *func_arg) {
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
  pthread->status = TASK_RUNNING; //修改线程的状态
  pthread->priority = prio;     //设置优先级
  //self_kstack是线程自己在内核态下使用的栈顶地址
  pthread->self_kstack = (uint32_t *)((uint32_t)pthread + PG_SIZE);
  pthread->stack_magic = 0x19870916;  //自定义的魔数
}

//创建一优先级为prio，名字为name，线程所执行的函数是function(func_arg)
//4个参数，name为线程名，prio为线程的优先级，要执行的函数是function，func_arg是函数function的参数。
struct task_struct *thread_start(char *name, int prio, thread_func function, void *func_arg) {
  //pcb都位于内核空间，包括用户进程的pcb也是在内核空间
  struct task_struct *thread = get_kernel_pages(1); //在内核空间中申请一页内存，get_kernel_page返回的是页的起始地址，故thread指向的是PCB的最低地址。

  init_thread(thread, name, prio);  //初始化刚刚创建的thread线程
  thread_create(thread, function, func_arg);

  //movl %0, %%esp，也就是使thread->self_kstack的值作为栈顶。
  //此时thread->self_kstack指向线程栈的最低处，这是在函数thread_create中设定的。
  //接下来4个弹栈操作：pop %%ebp;pop %%ebx;pop %%edi;pop %%esi使之前初始化的0弹入到相应寄存器中。
  //接下来执行ret，ret 会把栈顶的数据作为返回地址送上处理器的EIP寄存器。
  //此时栈顶就是在thread_create中为kthread_stack->eip所赋的值kernel_thread。
  //因此，在执行ret后，处理器会去执行kernel_thread函数。
  //接着在kernel_thread函数中会调用传给函数function(func_arg)。
  asm volatile ("movl %0, %%esp; pop %%ebp; pop %%ebx; pop %%edi; pop %%esi; ret" : : "g" (thread->self_kstack) : "memory");

  return thread;
}