#ifndef __THREAD_THREAD_H
#define __THREAD_THREAD_H
#include "stdint.h"
#include "list.h"
#include "bitmap.h"
#include "memory.h"

#define MAX_FILES_OPEN_PER_PROC 8
#define TASK_NAME_LEN 16

//自定义通用函数类型，它将在很多线程函数中作为形参类型
typedef void thread_func(void *); //用来指定在线程中运行的函数类型
typedef int16_t pid_t;

//进程或线程的状态
//进程与线程的区别是它们是否独自拥有地址空间，也就是是否拥有页表，程序的状态都是通用的
enum task_status {
  TASK_RUNNING, //任务正在运行
  TASK_READY,   //任务已经准备好，可以被调度执行
  TASK_BLOCKED, //任务被阻塞，等待某些外部事件的发生
  TASK_WAITING, //任务正在等待某些资源的释放
  TASK_HANGING, //任务已经挂起，等待被唤醒
  TASK_DIED     //任务已经结束或被终止
};

//***********   中断栈intr_stack   ***********
//此结构用于中断发生时保护程序(线程或进程)的上下文环境:
//进程或线程被外部中断或软中断打断时,会按照此结构压入上下文
//寄存器,intr_exit中的出栈操作是此结构的逆操作
//此栈在线程自己的内核栈中位置固定,所在页的最顶端

//定义程序的中断栈，此结构用于中断发生时保护程序的上下文环境，进入中断后，
//在kernel.S中的中断入口程序“intr%1entry”所执行的上下文保护的一系列压栈操作都是压入了此结构中。
//kernel.S中intr_exit中的出栈操作便是此结构的逆操作。
struct intr_stack {   
  uint32_t vec_no;  //kernel.S宏VECTOR中push %1压入的中断号
  uint32_t edi;
  uint32_t esi;
  uint32_t ebp;
  uint32_t esp_dummy;
  //虽然pushad把esp也压入，但esp是不断变化的，所以会被popad忽略
  uint32_t ebx;
  uint32_t edx;
  uint32_t ecx;
  uint32_t eax;
  uint32_t gs;
  uint32_t fs;
  uint32_t es;
  uint32_t ds;

  //以下由cpu从低特权级进入高特权级时压入
  uint32_t err_code;  //err_code会被压入在eip之后
  void (*eip) (void);
  uint32_t cs;
  uint32_t eflags;
  void *esp;
  uint32_t ss;
};

//**********  线程栈thread_stack  ***********
//线程自己的栈,用于存储线程中待执行的函数
//此结构在线程自己的内核栈中位置不固定,
//用在switch_to时保存线程环境。
//实际位置取决于实际运行情况。

//struct thread_stack定义了线程栈，此栈有2个作用，主要就是体现在第5个成员eip上。
//（1）线程是使函数单独上处理器运行的机制，因此线程肯定得知道要运行哪个函数，首
//次执行某个函数时，这个栈就用来保存待运行的函数，其中eip便是该函数的地址。
//（2）将来咱们是用switch_to函数实现任务切换，当任务切换时，此eip用于保存任务切换后的新任务的返回地址。
struct thread_stack {
  //在函数调用时，ABI规定ebp、ebx、edi、esi、和esp归主调函数所用，其余的寄存器归被调函数所用。
  //不管被调函数中是否使用了这5个寄存器，在被调函数执行完后，这5个寄存器的值不该被改变。
  //要在汇编中保存这5个寄存器的值，这个结构是为switch_to函数准备的，C函数schedule会调用switch_to
  //esp的值会由调用约定来保证
  uint32_t ebp;
  uint32_t ebx;
  uint32_t edi;
  uint32_t esi;

  //线程第一次执行时，eip指向待调用的函数kernel_thread
  //其他时候，eip是指向switch_to的返回地址
  void (*eip) (thread_func *func, void *func_arg);

  //*****  以下仅供第一次被调度上cpu时使用  *****

  //kernel_thread函数并不是通过调用call指令的形式执行的，而是咱们用汇编指令ret“返回”执行的
  //函数一般用call调用，栈顶是返回地址，这样可以正确获取参数，但是通过ret调用，并未压入返回地址，
  //所以此处需要uinused_retaddr进行占位，为了kernel_thread函数可以正确获取参数

  //参数unused_ret只为占位置充数为返回地址
  void (*unused_retaddr); //用来充当返回地址，在返回地址所在的栈帧占个位置，因此unused_retaddr中的值并不重要，仅仅起到占位的作用。
  thread_func *function;    //由kernel_thread所调用的函数名
  void *func_arg;           //由kernel_thread所调用的函数所需的参数
};

//进程或线程的pcb
struct task_struct {
  uint32_t *self_kstack;    //各内核线程都使用自己的内核栈
  pid_t pid;                //线程pid
  enum task_status status;  //记录线程状态
  char name[16];            //记录任务（线程或进程）的名字
  uint8_t priority;         //线程优先级
  uint8_t ticks;            //每次在处理器上执行的时间嘀嗒数(任务的时间片)，每次时钟中断都会将当前任务的ticks减1，当减到0时就被换下处理器

  //此任务自上cpu运行后至今占用了多少cpu嘀嗒数，也就是此任务执行了多久
  uint32_t elapsed_ticks;

  int32_t fd_table[MAX_FILES_OPEN_PER_PROC];    //文件描述符数组

  //以下两个结构将来从队列中把它们取出来时，还需要再通过offset宏与elem2entry宏的“反操作”，
  //实现从&general_tag到&thread的地址转换，将它们还原成线程的PCB地址后才能使用

  //general_tag的作用是用于线程在一般的队列中的结点
  //线程的标签，当线程被加入到就绪队列thread_ready_list或其他等待队列中时，就把该线程PCB中general_tag的地址加入队列。
  struct list_elem general_tag;

  //all_list_tag的作用是用于线程队列thread_all_list中的结点，专用于线程被加入全部线程队列时使用
  struct list_elem all_list_tag;

  //进程自己页表的虚拟地址,如果该任务为线程，pgdir则为NULL，否则pgdir会被赋予页表的虚拟地址，
  //注意此处是虚拟地址，页表加载时还是要被转换成物理地址的
  uint32_t *pgdir;          //进程自己页表的虚拟地址
  //每个进程都拥有4GB的虚拟地址空间，虚拟地址连续而物理地址可以不连续，所以需要一个虚拟地址池
  struct virtual_addr userprog_vaddr; //用户进程的虚拟地址池
  struct mem_block_desc u_block_desc[DESC_CNT];   //用户进程内存块描述符
  uint32_t cwd_inode_nr;    //进程所在的工作目录的inode编号
  int16_t parent_pid;       //父进程pid
  int8_t exit_status;       //进程结束时自己调用exit传入的参数
  uint32_t stack_magic;     //栈的边界标记，用于检测溢出，防止压栈过程中会把PCB中的信息给覆盖
};

extern struct list thread_ready_list;
extern struct list thread_all_list;

void thread_create(struct task_struct* pthread, thread_func function, void* func_arg);
void init_thread(struct task_struct* pthread, char* name, int prio);
struct task_struct* thread_start(char* name, int prio, thread_func function, void* func_arg);
struct task_struct* running_thread(void);
void schedule(void);
void thread_init(void);
void thread_block(enum task_status stat);
void thread_unblock(struct task_struct *pthread);
void thread_yield(void);
pid_t fork_pid(void);
void sys_ps(void);
void thread_exit(struct task_struct* thread_over, bool need_schedule);
struct task_struct* pid2thread(int32_t pid);
void release_pid(pid_t pid);

#endif