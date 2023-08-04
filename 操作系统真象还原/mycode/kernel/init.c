#include "init.h"
#include "print.h"
#include "interrupt.h"
#include "../device/timer.h"
#include "memory.h"
#include "thread.h"
#include "console.h"

//初始化所有模块
void init_all() {
  put_str("init_all\n");
  idt_init(); //初始化中断
  timer_init(); //初始化PIT
  mem_init();	  // 初始化内存管理系统
  thread_init();  //初始化线程相关结构
  console_init(); //控制台初始化最好放在开中断之前
}