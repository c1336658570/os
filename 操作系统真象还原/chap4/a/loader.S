   %include "boot.inc"
   section loader vstart=LOADER_BASE_ADDR
   LOADER_STACK_TOP equ LOADER_BASE_ADDR  ;用于loader在保护模式下的栈
   jmp loader_start					;机器码是E91702，3字节大小。此处的物理地址是:       当前指令的大小+gdt大小+60个预留描述符+gdt_ptr的6字节+loadermsg的17个字节
   ;jmp LOADER_BASE_ADDR+ 0xb17  ; 等价于上面那条指令，详细请看书165页              3+32+480+6+17=538=0x21a  0x900+0x21a=0xb1a  0xb1a-3=0xb17
;构建gdt及其内部的描述符   0号段描述符，不可用
   GDT_BASE:   dd    0x00000000 
	       dd    0x00000000
;代码段描述符
   CODE_DESC:  dd    0x0000FFFF  ;低4字节，0-15位（段界限0-15位），16-31位（段基址0-15位）
	       dd    DESC_CODE_HIGH4
;数据段和栈段的段描述符，数据段和栈段共同使用一个段描述符   段描述符的向上或向下拓展属性，是CPU在检查段界限时使用的，向上拓展的段同样能做栈段，只是访问内存时，段界限的检查按照向上拓展的段的方式检查。
   DATA_STACK_DESC:  dd    0x0000FFFF
		     dd    DESC_DATA_HIGH4
;显存段描述符  未采用平坦模型，其他段都采用平坦模型   段基址0xb8000
   VIDEO_DESC: dd    0x80000007	       ;limit=(0xbffff-0xb8000)/4k=0x7
	       dd    DESC_VIDEO_HIGH4  ; 此时dpl已改为0

   GDT_SIZE   equ   $ - GDT_BASE ;GDT大小
   GDT_LIMIT   equ   GDT_SIZE -	1  ;GDT界限
   times 60 dq 0	; 此处预留60个描述符的slot  往GDT中添加其他描述符，提前保留空间而已。
   SELECTOR_CODE equ (0x0001<<3) + TI_GDT + RPL0    ; 相当于(CODE_DESC - GDT_BASE)/8 + TI_GDT + RPL0
   SELECTOR_DATA equ (0x0002<<3) + TI_GDT + RPL0	 ; 同上  栈段段选择子也用这个选择子（数据段段选择子）
   SELECTOR_VIDEO equ (0x0003<<3) + TI_GDT + RPL0	 ; 同上 

   ;以下是定义全局描述符表gdt的指针，前2字节是gdt界限，后4字节是gdt起始地址

   gdt_ptr  dw  GDT_LIMIT 
	    dd  GDT_BASE
   loadermsg db '2 loader in real.' ;该字符串用来“显示”一下要进入保护模式了，长度17。

   loader_start:

;打印字符，"2 LOADER"说明loader已经成功加载
; 输出背景色绿色，前景色红色，并且跳动的字符串"1 MBR"
mov byte [gs:160],'2'
mov byte [gs:161],0xA4     ; A表示绿色背景闪烁，4表示前景色为红色

mov byte [gs:162],' '
mov byte [gs:163],0xA4

mov byte [gs:164],'L'
mov byte [gs:165],0xA4   

mov byte [gs:166],'O'
mov byte [gs:167],0xA4

mov byte [gs:168],'A'
mov byte [gs:169],0xA4

mov byte [gs:170],'D'
mov byte [gs:171],0xA4

mov byte [gs:172],'E'
mov byte [gs:173],0xA4

mov byte [gs:174],'R'
mov byte [gs:175],0xA4




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
   mov	 dx, 0x1800		 ; 行数dh为0x18，列数dl为0x00。这是int 0x10的参数。
   int	 0x10                    ; 10h 号中断

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
   or eax, 0x00000001      ;将PE位置为1
   mov cr0, eax
;自105行之后，CPU便进入了保护模式，故第107行的指令已经是在保护模式下了，但它依然还是16位的指令，
;相当于处于16位保护模式下。为了让其使用32位偏移地址，所以添加了伪指令dword，故其机器码前会加0x66反转前缀。
;描述符缓冲寄存器在实模式下时已经在使用了，其低20位是段基址，但其他位默认为0，也就是描述符中的D位为0，
;这表示当前的操作数大小是16位。因为当前段描述符缓冲寄存器中的D位是0，所以操作数是16位，
;当前属于16位保护模式。故，在这里也可以把 dword 去掉，毕竟当前操作数大小就是16位。而且p_mode_start的地址并没有超过16位
   ;jmp dword SELECTOR_CODE:p_mode_start	     ; 刷新流水线，避免分支预测的影响,这种cpu优化策略，最怕jmp跳转，  66ea4b0b00000800  jmp far 0008：00000b4b
   jmp  SELECTOR_CODE:p_mode_start	     ; 刷新流水线，避免分支预测的影响,这种cpu优化策略，最怕jmp跳转，    ea480b0800    jmp far 0008：0b48
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
;往显存第 80 个字符的位置（第 2 行首字符的位置）写入字符 P。默认的文本显示模式是
;80*25，即每行是 80 个字符（0～79），每个字符占 2 字节，故传入偏移地址是 80*2=160。
   mov byte [gs:320], 'P'  ;没有传入属性值，便会默认为黑底白字。

   jmp $
