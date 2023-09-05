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
#include "ide.h"
#include "stdio-kernel.h"
#include "string.h"

void init(void);

int main(void) {
   put_str("I am kernel\n");
   init_all();

/*************    写入应用程序    *************/
  uint32_t file_size = 23964;
  uint32_t sec_cnt = DIV_ROUND_UP(file_size, 512);
  struct disk* sda = &channels[0].devices[0];
  void* prog_buf = sys_malloc(sec_cnt * SECTOR_SIZE);
  memset(prog_buf, 0, sec_cnt * SECTOR_SIZE);
  ide_read(sda, 220, prog_buf, sec_cnt);
  int32_t fd1 = sys_open("/prog_no_arg", O_CREAT|O_RDWR);
  if (fd1 != -1) {
    if(sys_write(fd1, prog_buf, file_size) == -1) {
      printk("file write error!\n");
      while(1);
    }
  }
  sys_close(fd1);
  sys_free(prog_buf);

  file_size = 24480;
  sec_cnt = DIV_ROUND_UP(file_size, 512);
  prog_buf = sys_malloc(sec_cnt * SECTOR_SIZE);
  memset(prog_buf, 0, sec_cnt * SECTOR_SIZE);
  ide_read(sda, 300, prog_buf, sec_cnt);
  int32_t fd2 = sys_open("/prog_arg", O_CREAT|O_RDWR);
  if (fd2 != -1) {
     if(sys_write(fd2, prog_buf, file_size) == -1) {
        printk("file write error!\n");
        while(1);
     }
  }
  sys_close(fd2);
  sys_free(prog_buf);

  file_size = 24784; 
  sec_cnt = DIV_ROUND_UP(file_size, 512);
  prog_buf = sys_malloc(sec_cnt * SECTOR_SIZE);
  memset(prog_buf, 0, sec_cnt * SECTOR_SIZE);
  ide_read(sda, 400, prog_buf, sec_cnt);
  int32_t fd3 = sys_open("/cat", O_CREAT|O_RDWR);
  if (fd3 != -1) {
      if(sys_write(fd3, prog_buf, file_size) == -1) {
        printk("file write error!\n");
        while(1);
      }
  }
  sys_close(fd3);
  sys_free(prog_buf);

  file_size = 974;
  sec_cnt = DIV_ROUND_UP(file_size, 512);
  prog_buf = sys_malloc(sec_cnt * SECTOR_SIZE);
  memset(prog_buf, 0, sec_cnt * SECTOR_SIZE);
  ide_read(sda, 500, prog_buf, sec_cnt);
  int32_t fd4 = sys_open("/cat.c", O_CREAT|O_RDWR);
  if (fd4 != -1) {
      if(sys_write(fd4, prog_buf, file_size) == -1) {
        printk("file write error!\n");
        while(1);
      }
  }
  sys_close(fd4);
  sys_free(prog_buf);

  file_size = 24408;
  sec_cnt = DIV_ROUND_UP(file_size, 512);
  prog_buf = sys_malloc(sec_cnt * SECTOR_SIZE);
  memset(prog_buf, 0, sec_cnt * SECTOR_SIZE);
  ide_read(sda, 600, prog_buf, sec_cnt);
  int32_t fd5 = sys_open("/prog_pipe", O_CREAT|O_RDWR);
  if (fd5 != -1) {
      if(sys_write(fd5, prog_buf, file_size) == -1) {
        printk("file write error!\n");
        while(1);
      }
  }
  sys_close(fd5);
  sys_free(prog_buf);

/*************    写入应用程序结束   *************/
   cls_screen();
   console_put_str("[rabbit@localhost /]$ ");
   thread_exit(running_thread(), true);
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
    int status;
    int child_pid;
    //init在此处不停地回收僵尸进程
    while(1) {
      child_pid = wait(&status);
      printf("I`m init, My pid is 1, I recieve a child, It`s pid is %d, status is %d\n", child_pid, status);
    }
  } else {            //子进程
    my_shell();
  }
  panic("init: should not be here");
}