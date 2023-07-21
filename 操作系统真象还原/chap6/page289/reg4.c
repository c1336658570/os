#include <stdio.h>
void main()
{
    int in_a = 0x12345678, in_b = 0;
    //往in_b中传入一个字，默认会传入低字，低16位：0x5678。
    asm("movw %w1,%0;":"=m"(in_b):"a"(in_a));
    printf("word in_b is 0x%x\n", in_b);
    in_b = 0;           //将in_b恢复为0,避免上次赋值造成混乱
    //movb 指令，往 in_b 中传入一个字节，默认会传入低字节，低 8 位：0x78。
    asm("movb %b1,%0;":"=m"(in_b):"a"(in_a));
    printf("low byte in_b is 0x%x\n", in_b);

    in_b = 0;           //将in_b恢复为0,避免上次赋值造成混乱
    //这次我们在占位符中用了'h'，所以 in_b 中应该是 in_a 的第 7～15 位：0x56。
    asm("movb %h1,%0;":"=m"(in_b):"a"(in_a));
    printf("high byte in_b is 0x%x\n", in_b);
}
