/*
io.h却是函数的实现并且，里面各函数的作用域都是static，这表明该函数仅在本文件中有效，对外不可见。
这意味着，凡是包含io.h的文件，都会获得一份io.h中所有函数的拷贝，也就是说同样功能的函数在程序中会存在多个副本。
这里的函数并不是普通的函数，它们都是对底层硬件端口直接操作的，通常由设备的驱动程序来调用，
为了快速响应，函数调用上需要更加高效。一般的函数调用需要涉及到现场保护及恢复现场，
即函数调用前要把相关的栈、返回地址（CS和EIP）保存到栈中，函数执行完返回后再将它们从栈中恢复到寄存器。
栈是低速内存，而且入栈出栈操作很多，速度很慢。因此，为了提速，在我们的实现中，函数的存储类型都是static，
并且加了inline关键字，它建议处理器将函数编译为内嵌的方式。
这样编译后的代码中将不包含call指令，也就不属于函数调用了，而是顺次执行。
*/

/**************	 机器模式   ***************
	 b -- 输出寄存器QImode名称,即寄存器中的最低8位:[a-d]l。
	 w -- 输出寄存器HImode名称,即寄存器中2个字节的部分,如[a-d]x。

	 HImode
	     “Half-Integer”模式，表示一个两字节的整数。 
	 QImode
	     “Quarter-Integer”模式，表示一个一字节的整数。 
*******************************************/ 

#ifndef __LIB_IO_H
#define __LIB_IO_H
#include "stdint.h"

//向端口port写入1字节
static inline void outb(uint16_t port, uint8_t data) {
  //N为立即数约束，它表示0～255的立即数，对端口指定N表示0～255，d表示用dx存储端口号，%b0表示对应al，%w1表示对应dx
  asm volatile ("outb %b0, %w1" : : "a"(data), "Nd"(port));
}

//将addr处起始的word_cnt个字写入端口port
static inline void outsw(uint16_t port, const void *addr, uint32_t word_cnt) {
  //+表示即是输入也是输出，S表示寄存器esi/si，c表示寄存器ecx，d表示dx
  //outsw把ds:esi处内容写入端口port，已经将ds,es,ss段的选择子都设置为相同的值了，此时不用担心数据错乱｡
  asm volatile ("cld; rep outsw" : "+S"(addr), "+c"(word_cnt) : "d"(port));
}

/* 将从端口port读入的一个字节返回 */
static inline uint8_t inb(uint16_t port) {
   uint8_t data;
   asm volatile ("inb %w1, %b0" : "=a" (data) : "Nd" (port));
   return data;
}


//从端口port读入word_cnt个字写入addr
static inline void insw(uint16_t port, void *addr, uint32_t word_cnt) {
  //insw是将从端口port处读入的16位内容写入es:edi指向的内存
  asm volatile ("cld; rep insw" : "+D"(addr), "+c"(word_cnt) : "d"(port) : "memory");
}

#endif