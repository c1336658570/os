#include "syscall-init.h"
#include "syscall.h"
#include "stdint.h"
#include "print.h"
#include "thread.h"

#define syscall_nr 32   //表示最大支持的系统调用子功能个数
typedef void *syscall;
syscall syscall_table[syscall_nr];

//返回当前任务的pid
uint32_t sys_getpid(void) {
  return running_thread()->pid;
}

//初始化系统调用，就是为数组syscall_table赋值
void syscall_init(void) {
  put_str("syscall_init start\n");
  syscall_table[SYS_GETPID] = sys_getpid;
  put_str("syscall_init done\n");
}