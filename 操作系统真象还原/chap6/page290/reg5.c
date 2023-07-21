#include <stdio.h>
void main() {
    int in_a = 18,in_b = 3,out = 0;
    /*
    第12行，被除数in_a通过寄存器约束a存入寄存器eax，除数in_b通过内存约束m被gcc将自己
    的内存地址传给汇编做操作数。，第10行除法指令divb可以通过%[divisor]引用除数所在
    的内存，进行除法运算。divb是8位除法指令，商存放在寄存器al中，余数存放在寄存器ah中。
    第10行中用movb指令将寄存器al的值写入用于存储结果的c变量out的地址中。
    */
    asm("divb %[divisor];movb %%al,%[result];"\
        :[result]"=m"(out) \
        :"a"(in_a),[divisor]"m"(in_b) \
       );
    printf("in_a = %d,in_b = %d,result = %d\n",in_a,in_b,out);
}
