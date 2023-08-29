/*
增加系统调用的步骤
（1）在 syscall.h 中的结构 enum SYSCALL_NR 里添加新的子功能号。
（2）在 syscall.c 中增加系统调用的用户接口。
（3）在 syscall-init.c 中定义子功能处理函数并在 syscall_table 中注册。
*/

#include "syscall.h"
#include "thread.h"

//无参数的系统调用
//大括号中最后一个语句的值会作为大括号代码块的返回值，而且要在最后一个语句后添加分号';'，否则编译时会报错。
#define _syscall0(NUMBER) ({  \
  int retval;                 \
  asm volatile (              \
    "int $0x80"               \
    : "=a" (retval)           \
    : "a" (NUMBER)            \
    : "memory"                \
  );                          \
  retval;                     \
})

//一个参数的系统调用
#define _syscall1(NUMBER, ARG1) ({    \
  int retval;                         \
  asm volatile (                      \
    "int $0x80"                       \
    : "=a" (retval)                   \
    : "a" (NUMBER), "b" (ARG1)        \
    : "memory"                        \
  );                                  \
  retval;                             \
})

//两个参数的系统调用
#define _syscall2(NUMBER, ARG1, ARG2) ({    \
   int retval;						                  \
   asm volatile (					                  \
   "int $0x80"						                  \
   : "=a" (retval)					                \
   : "a" (NUMBER), "b" (ARG1), "c" (ARG2)   \
   : "memory"						                    \
   );							                          \
   retval;						                      \
})

//三个参数的系统调用
#define _syscall3(NUMBER, ARG1, ARG2, ARG3) ({		          \
   int retval;						                                  \
   asm volatile (					                                  \
      "int $0x80"					                                  \
      : "=a" (retval)					                              \
      : "a" (NUMBER), "b" (ARG1), "c" (ARG2), "d" (ARG3)    \
      : "memory"					                                  \
   );							                                          \
   retval;						                                      \
})

uint32_t getpid() {
  return _syscall0(SYS_GETPID);
}

//把buf中count个字符写入文件描述符fd
uint32_t write(int32_t fd, const void* buf, uint32_t count) {
  return _syscall3(SYS_WRITE, fd, buf, count);
}

//申请size字节大小的内存，并返回结果
void *malloc(uint32_t size) {
  return (void *)_syscall1(SYS_MALLOC, size);
}

//释放ptr指向的内存
void free(void *ptr) {
  _syscall1(SYS_FREE, ptr);
}

//派生子进程,返回子进程pid
pid_t fork(void){
  return _syscall0(SYS_FORK);
}

//从文件描述符fd中读取count个字节到buf
int32_t read(int32_t fd, void* buf, uint32_t count) {
  return _syscall3(SYS_READ, fd, buf, count);
}

//输出一个字符
void putchar(char char_asci) {
  _syscall1(SYS_PUTCHAR, char_asci);
}

//清空屏幕
void clear(void) {
  _syscall0(SYS_CLEAR);
}