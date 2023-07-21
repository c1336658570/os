#include <stdio.h>

int main()
{
    int in_a=1,in_b=2;
    printf("in_a is %d,in_b is %d\n",in_a,in_b);
    /*
    变量 in_b 用 in_a 的值替换。in_b 最终变成 1。
    把 in_a 施加寄存器约束 a，告诉 gcc 把变量 in_a 放到寄存器 eax 中，对 in_b 施
    加内存约束 m，告诉 gcc 把变量 in_b 的指针作为内联代码的操作数。
    “%1”，它是序号占位符，在这里大家认为它代表 in_b 的内存地址（指针）就行了
    %b0，这是用的 32 位数据的低 8 位，在这里就是指 al 寄存器。
    如果不显式加字符'b'，编译器也会按照低 8 位来处理，但它会发出警告。
    */
    asm("movb %b0,%1;"::"a"(in_a),"m"(in_b));
    printf("now in_a is %d,in_b is %d\n",in_a,in_b);
    return 0;
}
/*
内存约束也不是乱用的，至少在 assembly code 中的指令得允许操作数是内存，比如
asm("movl %0, %1;"::"m"(in_a),"m"(in_b))就会出问题。movl 指令不允许“内存”到“内存”的复制，编译
阶段就会报错。
*/
