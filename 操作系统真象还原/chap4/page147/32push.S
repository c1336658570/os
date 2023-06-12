   %include "boot.inc"
   section loader vstart=LOADER_BASE_ADDR
   LOADER_STACK_TOP equ LOADER_BASE_ADDR
   jmp loader_start					; 此处的物理地址是:
   
;构建gdt及其内部的描述符
   GDT_BASE:   dd    0x00000000 
	       dd    0x00000000

   CODE_DESC:  dd    0x0000FFFF 
	       dd    DESC_CODE_HIGH4

   DATA_STACK_DESC:  dd    0x0000FFFF
		     dd    DESC_DATA_HIGH4

   VIDEO_DESC: dd    0x80000007	       ;limit=(0xbffff-0xb8000)/4k=0x7
	       dd    DESC_VIDEO_HIGH4  ; 此时dpl已改为0

   GDT_SIZE   equ   $ - GDT_BASE
   GDT_LIMIT   equ   GDT_SIZE -	1 
   times 60 dq 0					 ; 此处预留60个描述符的slot
   SELECTOR_CODE equ (0x0001<<3) + TI_GDT + RPL0         ; 相当于(CODE_DESC - GDT_BASE)/8 + TI_GDT + RPL0
   SELECTOR_DATA equ (0x0002<<3) + TI_GDT + RPL0	 ; 同上
   SELECTOR_VIDEO equ (0x0003<<3) + TI_GDT + RPL0	 ; 同上 

   ;以下是定义gdt的指针，前2字节是gdt界限，后4字节是gdt起始地址

   gdt_ptr  dw  GDT_LIMIT 
	    dd  GDT_BASE
   loadermsg db '2 loader in real.'

   loader_start:

;------------------------------------------------------------
;INT 0x10    功能号:0x13    功能描述:打印字符串
;------------------------------------------------------------
;输入:
;AH 子功能号=13H
;BH = 页码
;BL = 属性(若AL=00H或01H)
;CX＝字符串长度
;(DH、DL)＝坐标(行、列)
;ES:BP＝字符串地址 
;AL＝显示输出方式
;   0——字符串中只含显示字符，其显示属性在BL中。显示后，光标位置不变
;   1——字符串中只含显示字符，其显示属性在BL中。显示后，光标位置改变
;   2——字符串中含显示字符和显示属性。显示后，光标位置不变
;   3——字符串中含显示字符和显示属性。显示后，光标位置改变
;无返回值
   mov	 sp, LOADER_BASE_ADDR
   mov	 bp, loadermsg           ; ES:BP = 字符串地址
   mov	 cx, 17			 ; CX = 字符串长度
   mov	 ax, 0x1301		 ; AH = 13,  AL = 01h
   mov	 bx, 0x001f		 ; 页号为0(BH = 0) 蓝底粉红字(BL = 1fh)
   mov	 dx, 0x1800		 ;
   int	 0x10                    ; INT 10h	显示服务 - 由BIOS或操作系统设定以供软件调用。
;	AH=00h	设定显示模式
;	AH=01h	设定游标形态
;	AH=02h	设定游标位置
;	AH=03h	获取游标位置与形态
;	AH=04h	获取光笔位置
;	AH=05h	设定显示页
;	AH=06h	清除或卷轴画面(上)
;	AH=07h	清除或卷轴画面(下)
;	AH=08h	读取游标处字符与属性
;	AH=09h	更改游标处字符与属性
;	AH=0Ah	更改游标处字符
;	AH=0Bh	设定边界颜色
;	AH=0Eh	在TTY模式下写字符
;	AH=0Fh	取得目前显示模式
;	AH=13h	写字符串


;----------------------------------------   准备进入保护模式   ------------------------------------------
									;1 打开A20
									;2 加载gdt
									;3 将cr0的pe位置1


   ;-----------------  打开A20  ----------------
   in al,0x92
   or al,0000_0010B
   out 0x92,al

   ;-----------------  加载GDT  ----------------
   lgdt [gdt_ptr]


   ;-----------------  cr0第0位置1  ----------------
   mov eax, cr0
   or eax, 0x00000001
   mov cr0, eax

   ;jmp dword SELECTOR_CODE:p_mode_start	     ; 刷新流水线，避免分支预测的影响,这种cpu优化策略，最怕jmp跳转，
   jmp  SELECTOR_CODE:p_mode_start	     ; 刷新流水线，避免分支预测的影响,这种cpu优化策略，最怕jmp跳转，
					     ; 这将导致之前做的预测失效，从而起到了刷新的作用。

[bits 32]
p_mode_start:
   mov ax, SELECTOR_DATA
   mov ds, ax
   mov es, ax
   mov ss, ax
   mov esp,LOADER_STACK_TOP
   mov ax, SELECTOR_VIDEO
   mov gs, ax

   mov byte [gs:160], 'P'
;                    下一条的指令的机器码    当前 esp 值
   push byte 0x7  ;  6a07                 0x00000900
   ;word 是伪指令，表示2字节大小的数据类型，66是反转操作数前缀,这是操作数大小反转前缀。
   ;编译器添加此反转前缀的原因是在32位下的操作数是4字节，此处要压入2字节，
   ;这是16位模式下的操作数尺寸。
   push word 0x8  ;  666a08               0x000008fc  0x900-0x8fc=4
   push dword 0x9 ;  6a09                 0x000008fa  0x8fc-0x8fa=2

   jmp $          ;  ebfe                 0x000008f6  0x8fa-0x8f6=4

   ;将110-117替换为如下四行演示保护模式下压入段寄存器，esp均会减4
   push cs        ;  0x0e                 0x900
   push ds        ;  0x1e  0x             0x8fc
   push es        ;  0x06                 0x8f8

   jmp $          ;  0xebfe               0x8f4

   ;将110-117替换为如下五行演示保护模式下压入通用寄存器和内存
   ;在保护模式每次压入16位数据时栈指针esp就减2，每次压入32位数据时栈指针esp就减4。
   push ax        ;  6650                 0x900
   push eax       ;  50                   0x8fe
   push word [0x1234];66ff3534120000      0x8fa
   push dword [0x1234];ff3534120000       0x8f8

   jmp $          ;ebfe                   0x8f4