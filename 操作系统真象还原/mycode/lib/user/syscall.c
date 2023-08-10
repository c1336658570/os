/*
增加系统调用的步骤
（1）在 syscall.h 中的结构 enum SYSCALL_NR 里添加新的子功能号。
（2）在 syscall.c 中增加系统调用的用户接口。
（3）在 syscall-init.c 中定义子功能处理函数并在 syscall_table 中注册。
*/

#include "syscall.h"

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