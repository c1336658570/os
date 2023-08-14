[bits 32]
%define ERROR_CODE nop  ;异常CPU自动压入错误码就不做操作
%define ZERO push 0     ;异常CPU未压入错误码就压入一个4字节0,方便统一格式

extern put_str          ;声明外部函数
extern idt_table        ;idt_table是C中注册的中断处理程序数组

section .data
intr_str db "interrupt occur!", 0xa, 0

global intr_entry_table
intr_entry_table:
%macro VECTOR 2   ;两个参数，第1个是中断向量号，第二个是压入错误玛的操作（nop或push 0）
section .text
intr%1entry:  ;因为宏会多次调用，展开，%1为了标号不重名   最终会变为intr[0～32]entry
  %2  ;压入错误玛0或什么都不干，为了统一格式，因为有的中断压入错误码，有的不压入，所以在此处理一下。
  ;保存上下文
  ;在此汇编文件中要调用C程序，一定会使当前寄存器环境破坏，所以要保存当前所使用的寄存器环境。
  push ds
  push es
  push fs
  push gs
  pushad    ;其入栈顺序是：EAX,ECX,EDX,EBX,ESP,EBP,ESI,EDI

  ;如果是从片进入的中断，需要给从片和主片都发送EOI
  ;往主片和从片中写入0x20，也就是写入EOI。这是8259A的操作控制字OCW2，
  ;其中第5位是EOI位，此位为1，其余位全为0，所以是0x20。
  mov al, 0x20
  out 0xa0, al
  out 0x20, al

  push %1
  call [idt_table + %1 * 4]   ;调用idt_table中的C版本中断处理函数
  jmp intr_exit

section .data
  dd intr%1entry

%endmacro

section .text
global intr_exit
intr_exit:
  ;恢复上下文
  add esp, 4    ;跳过中断号
  popad
  pop gs
  pop fs
  pop es
  pop ds
  add esp, 4    ;跳过error_code
  iretd

;定33个中断处理程序  0～19为处理器内部固定的异常类型  20～31是Intel保留的，32是时钟中断
VECTOR 0x00,ZERO
VECTOR 0x01,ZERO
VECTOR 0x02,ZERO
VECTOR 0x03,ZERO 
VECTOR 0x04,ZERO
VECTOR 0x05,ZERO
VECTOR 0x06,ZERO
VECTOR 0x07,ZERO 
VECTOR 0x08,ERROR_CODE
VECTOR 0x09,ZERO
VECTOR 0x0a,ERROR_CODE
VECTOR 0x0b,ERROR_CODE 
VECTOR 0x0c,ZERO
VECTOR 0x0d,ERROR_CODE
VECTOR 0x0e,ERROR_CODE
VECTOR 0x0f,ZERO 
VECTOR 0x10,ZERO
VECTOR 0x11,ERROR_CODE
VECTOR 0x12,ZERO
VECTOR 0x13,ZERO 
VECTOR 0x14,ZERO
VECTOR 0x15,ZERO
VECTOR 0x16,ZERO
VECTOR 0x17,ZERO 
VECTOR 0x18,ERROR_CODE
VECTOR 0x19,ZERO
VECTOR 0x1a,ERROR_CODE
VECTOR 0x1b,ERROR_CODE 
VECTOR 0x1c,ZERO
VECTOR 0x1d,ERROR_CODE
VECTOR 0x1e,ERROR_CODE
VECTOR 0x1f,ZERO 
VECTOR 0x20,ZERO    ;时钟中断
;因为bochs硬件模拟不全的原因，在此处多添加几个中断
VECTOR 0x21, ZERO   ;键盘中断对应的入口
VECTOR 0x22, ZERO   ;级联用的
VECTOR 0x23, ZERO   ;串口2对应的入口
VECTOR 0x24, ZERO   ;串口1对应的入口
VECTOR 0x25, ZERO   ;并口2对应的入口
VECTOR 0x26, ZERO   ;软盘对应的入口
VECTOR 0x27, ZERO   ;并口1对应的入口
VECTOR 0x28, ZERO   ;实时时钟对应的入口
VECTOR 0x29, ZERO   ;重定向
VECTOR 0x2a, ZERO   ;保留
VECTOR 0x2b, ZERO   ;保留
VECTOR 0x2c, ZERO   ;ps/2鼠标
VECTOR 0x2d, ZERO   ;fpu浮点单元异常
VECTOR 0x2e, ZERO   ;硬盘
VECTOR 0x2f, ZERO   ;保留
VECTOR 0x30, ZERO

;;;;;;;;;;;;;;;;0x80号中断;;;;;;;;;;;;;;;;
[bits 32]
;声明了外部数据结构syscall_table，syscall_table是个数组，数组成员是系统调用中子功能对应的处理函数
extern syscall_table
section .text
global syscall_handler  ;syscall_handler的定义
syscall_handler:
  ;1保存上下文环境
  push 0      ;压入0，使栈中格式统一    占位，为了使用intr_exit

  push ds
  push es
  push fs
  push gs
  pushad      ;入栈顺序：EAX,ECX,EDX,EBX,ESP,EBP,ESI,EDI
  
  push 0x80   ;此位置压入0x80也是为了保持统一的栈格式   占位，为了使用intr_exit

  ;2为系统调用子功能传入参数  只支持3个参数的系统调用，故只压入了三个参数
  ;按照C调用约定，最右边的参数先入栈，因此先把edx中的第3个参数入栈
  push edx    ;系统调用中第3个参数
  push ecx    ;系统调用中第2个参数
  push ebx    ;系统调用中第1个参数
  
  ;3调用子功能处理函数
  call [syscall_table + eax * 4]
  add esp, 12 ;跨过上面的三个参数

  ;4将call调用后的返回值存入当前内核栈中eax的位置
  ;经过上面的call函数调用，如果有返回值的话，eax的值已经变成了返回值（如果没有返回值也没关系，编译器会保证函数返回后eax的值不变）
  mov [esp + 8 * 4], eax  ;把寄存器eax的值回写到内核栈中用于保存eax的内存处
  jmp intr_exit     ;intr_exit返回，恢复上下文