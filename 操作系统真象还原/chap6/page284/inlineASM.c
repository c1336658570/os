//变量count和str都定义为全局变量。在基本内联汇编中，若要引用C变量，只能将它定义为全局变量。
//如果定义为局部变量，链接时会找不到这两个符号，这就是基本内联汇编的局限性
char* str = "hello inlineasm\n";
int count = 0;
int main() {
	//调用write系统调用，打印str
	//AT&T中的汇编指令是pusha（Intel 中的是pushad）
	asm("pusha;\				
		movl $4,%eax; \
		movl $1, %ebx; \
		movl str,%ecx; \
		movl $16,%edx; \
		int $0x80; \
		mov %eax,count; \
		popa; \
		");
	//asm(“movl $9,%eax;””pushl %eax”)		正确
	//asm(“movl $9,%eax””pushl %eax”)			错误 多条指令之间少了;
}

/*
gcc -o inlineASM.bin inlineASM.c -m32
./inlineASM.bin
*/