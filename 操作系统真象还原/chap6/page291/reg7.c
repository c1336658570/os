#include <stdio.h>
//call printf会段错误
//正常情况下printf会返回打印的字符数，”hello，world\n”共12个字符。
void main() {
  int ret_cnt = 0, test = 0;
  char *fmt = "hello,world\n";    //12个字符
  /*
  第19行通过内存约束m把字符串指针fmt传给了汇编代码，把变量test用寄存器约束r声明，由gcc自由分配寄存器。
  第14-15行调用printf。将定义的字符串指针fmt通过压栈传参给printf函数，在第15行执行会
  屏幕会打印hello，world换行。在此，eax 寄存器中值是printf的返回值，应该为12。
  在第16行回收参数所占的栈空间。第17行把立即数6传入了gcc为变量 test分配的寄存器。在第18行，
  output部分中的c变量ret_cnt获得了寄存器的值。
  */
  asm ("pushl %1;       \
  call printf;          \
  addl $4, %%esp;       \
  movl $6, %2"          \
  :"=a"(ret_cnt)        \
  :"m"(fmt), "r"(test)  \
  );
  printf("the number of bytes written is %d\n", ret_cnt);
}