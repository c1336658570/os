#include "timer.h"
#include "io.h"
#include "print.h"
#include "interrupt.h"
#include "thread.h"
#include "debug.h"

#define IRQ0_FREQUENCY 100          //设置的时钟中断的频率，将它设为100Hz
#define INPUT_FREQUENCY 1193180     //计数器0的工作脉冲信号频率
#define COUNTER0_VALUE INPUT_FREQUENCY / IRQ0_FREQUENCY   //计数器0的计数初值(1193180/中断信号的频率=计数器0的初始计数值)
#define CONTRER0_PORT 0x40          //计数器0的端口号0x40
#define COUNTER0_NO 0                //用在控制字中选择计数器的号码，其值为0，代表计数器0
#define COUNTER_MODE 2        //工作模式的代码，其值为2，即方式2，这是我们选择的工作方式：比率发生器。
#define READ_WRITE_LATCH 3    //读写方式，其值为3，这表示先读写低8位，再读写高8位。
#define PIT_CONTROL_PORT 0X43 //控制字寄存器的端口。

uint32_t ticks;   //ticks是内核自中断开启以来总共的嘀嗒数

//把操作的计数器counter_no､读写锁属性rwl､ 计数器模式counter_mode写入模式控制寄存器并赋予初始值counter_value
//counter_port是计数器的端口号，用来指定初值counter_value的目的端口号。
//counter_no用来在控制字中指定所使用的计数器号码，对应于控制字中的SC1和SC2位。
//rwl用来设置计数器的读/写/锁存方式，对应于控制字中的RW1和RW0位。
//counter_mode用来设置计数器的工作方式，对应于控制字中的M2～M0位。
//counter_value用来设置计数器的计数初值，由于此值是16位，所以我们用了uint16_t来定义它。
static void frequency_set(uint8_t counter_port, uint8_t counter_no, uint8_t rwl, \
                          uint8_t counter_mode, uint16_t counter_value) {
  //往控制字寄存器端口0x43中写入控制字
  outb(PIT_CONTROL_PORT, (uint8_t)(counter_no << 6 | rwl << 4 | counter_mode << 1));
  //先写入counter_value的低8位
  outb(counter_port, (uint8_t)counter_value);
  //再写入counter_value的高8位
  outb(counter_port, (uint8_t)0);
}

//时钟的中断处理函数
static void intr_timer_handler(void) {
  struct task_struct *cur_thread = running_thread();  //获取当前正在运行的线程

  ASSERT(cur_thread->stack_magic == 0x19870916);  //检查栈是否溢出

  //将线程总执行的时间加1
  cur_thread->elapsed_ticks++;  //记录此线程占用的cpu时间
  //将系统运行时间加1，实际上这就是中断发生的次数
  ticks++;    //从内核第一次处理时间中断后开始至今的滴哒数，内核态和用户态总共的嘀哒数

  if (cur_thread->ticks == 0) { //若进程时间片用完，就开始调度新的进程上cpu
    schedule();
  } else {  //将当前进程的时间片-1
    cur_thread->ticks--;
  }
}

void timer_init() {
  put_str("timer_init start\n");
  frequency_set(CONTRER0_PORT, COUNTER0_NO, READ_WRITE_LATCH, COUNTER_MODE, COUNTER0_VALUE);
  register_handler(0x20, intr_timer_handler); //注册时钟中断处理程序
  put_str("timer_init done\n");
}