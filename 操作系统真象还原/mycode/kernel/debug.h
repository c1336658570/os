#ifndef __KERNEL_DEBUG_H
#define __KERNEL_DEBUG_H
void panic_spin(char *filename, int line, const char *func, const char *condition);

//__VA_ARGS__ 是预处理器所支持的专用标识符。代表所有与省略号相对应的参数。"..."表示定义的宏其参数可变。
//__FILE__，__LINE__，__func__，这三个是预定义的宏，分别表示被编译的文件名、被编译文件中的行号、被编译的函数名。
#define PANIC(...) panic_spin(__FILE__, __LINE__, __func__, __VA_ARGS__)

//字符’#’的作用是让预处理器把CONDITION转换成字符串常量。
//比如CONDITION若为var!=0，#CONDITION的效果是变成了字符串“var!=0”。
//于是，传给panic_spin函数的第4个参数__VA_ARGS__，实际类型为字符串指针。
#ifdef NDEBUG
  #define ASSERT(CONDITION) ((void)0)
#else
  #define ASSERT(CONDITION) \
  if (CONDITION) {          \
                            \
  } else {                  \
    PANIC(#CONDITION);      \
  }                         \

#endif

#endif