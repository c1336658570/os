#include "print.h"
#include "init.h"
#include "thread.h"
#include "interrupt.h"
#include "console.h"
#include "process.h"
#include "syscall-init.h"
#include "syscall.h"
#include "stdio.h"
#include "memory.h"
#include "dir.h"
#include "fs.h"
#include "assert.h"
#include "shell.h"

void init(void);

int main(void) {
  put_str("I am kernel\n");
  init_all();
  cls_screen();
  console_put_str("[cccmmf@cccmmf:/]$ ");
  while(1);
  return 0;
}

//init进程
/*
init是用户级进程，因此咱们要调用process_execute创建进程，但由谁来创建init进程呢？
大伙儿知道，pid是从1开始分配的，init的pid是1，因此咱们得早早地创建init进程，抢夺1号pid。
目前系统中有主线程，其pid为1，还有ilde线程，其pid为2，因此咱们应该在创建主线程的函数make_main_thread
之前创建init，也就是在函数thread_init中完成
*/
void init(void) {
  uint32_t ret_pid = fork();
  if(ret_pid) {       //父进程
    while(1);
  } else {            //子进程
    my_shell();
  }
  panic("init: should not be here");
}