32push.S用来解释保护模式压栈
在保护模式下，同样是这些压入立即数的指令，栈指针会有怎样的变化呢？
当压入 8 位立即数时，由于保护模式下默认操作数是 32 位，CPU 将其扩展为 32 位后入栈，
esp 指针减 4。
当压入 16 位立即数时，CPU 直接压入 2 字节，esp 指针减 2。
当压入 32 位立即数时，CPU 直接压入 4 字节，esp 指针减 4。

对于段寄存器的入栈，即 cs、ds、es、fs、gs、ss，无论在哪种模式下，都是按当前模式的默认操作
数大小压入的。例如，在 16 位模式下，CPU 直接压入 2 字节，栈指针 sp 减 2。在 32 位模式下，CPU 直接压入 4 字节，栈指针 esp 减 4。

对于通用寄存器和内存，无论是在实模式或保护模式：
如果压入的是 16 位数据，栈指针减 2。
如果压入的是 32 位数据，栈指针减 4。