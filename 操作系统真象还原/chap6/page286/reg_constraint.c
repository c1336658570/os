//拓展内联汇编
#include <stdio.h>
int 
main()
{
    int in_a=1,in_b=333,out_sum;
    /*
    in_a 和 in_b 是在 input 部分中输入的，用约束名 a 为 c 变量 in_a 指定了
    用寄存器 eax，用约束名 b 为 c 变量 in_b 指定了用寄存器 ebx。addl 指令的结果存放到了寄存器 eax 中，
    在 output 中用约束名 a 指定了把寄存器 eax 的值存储到 c 变量 out_sum 中。output 中的'='号是操作数类型
    修饰符，表示只写，其实就是 out_sum=eax 的意思。
    */
    asm("addl %%ebx,%%eax":"=a"(out_sum):"a"(in_a),"b"(in_b));
    printf("sum is %d = %d + %d\n",out_sum, in_a, in_b);
    return 0;
}
