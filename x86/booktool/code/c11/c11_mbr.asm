         ;代码清单11-1
         ;文件名：c11_mbr.asm
         ;文件说明：硬盘主引导扇区代码 
         ;创建日期：2011-5-16 19:54

         ;设置堆栈段和栈指针 
         mov ax,cs      ;0x0000
         mov ss,ax
         mov sp,0x7c00
      
         ;计算GDT所在的逻辑段地址 
         mov ax,[cs:gdt_base+0x7c00]        ;低16位 
         mov dx,[cs:gdt_base+0x7c00+0x02]   ;高16位 
         mov bx,16        
         div bx            ;求段地址
         mov ds,ax                          ;令DS指向该段以进行操作
         mov bx,dx                          ;段内起始偏移地址 
      
         ;创建0#描述符，它是空描述符，这是处理器的要求
         mov dword [bx+0x00],0x00
         mov dword [bx+0x04],0x00  

         ;创建#1描述符，保护模式下的代码段描述符
         mov dword [bx+0x08],0x7c0001ff      ;线性地址0x00007c00，G=0粒度是字节（段界限以字节为单位），段界限0x001FF(512字节)，s=1属于存储器的段，当s=0时，表示是一个系统段
         mov dword [bx+0x0c],0x00409800      ;D=1是一个32位的段，P=1在内存中，DPL=00特权级为0，TYPE=1000，只能执行的代码段

         ;创建#2描述符，保护模式下的数据段描述符（文本模式下的显示缓冲区） 
         mov dword [bx+0x10],0x8000ffff      ;线性地址0x000b8000，G=0粒度是字节，段界限0x0FFFF(64KB)，s=1属于存储器的段
         mov dword [bx+0x14],0x0040920b      ;D=1是一个32位的段，P=1在内存中，DPL=00特权级为0，TYPE=0010，可读可写向上拓展的数据段

         ;创建#3描述符，保护模式下的堆栈段描述符
         mov dword [bx+0x18],0x00007a00      ;线性地址0x00000000，G=0粒度是字节，段界限0x07A00，s=1属于存储器的段
         mov dword [bx+0x1c],0x00409600      ;D=1是一个32位的段，P=1在内存中，DPL=00特权级为0，TYPE=0010，可读可写向下拓展的数据段，这里是栈段

         ;初始化描述符表寄存器GDTR
         mov word [cs: gdt_size+0x7c00],31  ;描述符表的界限（总字节数减一）   
         ;设置GDTR寄存器（全局段描述符表寄存器）                                    
         lgdt [cs: gdt_size+0x7c00]     ;操作数是6字节，低16位是GDT界限值(31)，高32位是GDT基地址(0x00007e00)。
      
         in al,0x92                     ;南桥芯片内的端口 ，读0x92端口，0x92第0位写1导致处理器复位，即重启，第1位用于控制A20
         or al,0000_0010B               ;将位1改为1，打开A20地址线     
         out 0x92,al                        ;打开A20,第21根地址线

         cli                                ;保护模式下中断机制尚未建立，实模式中断不再适用，BIOS中断也不适用，应 
                                            ;禁止中断 
         mov eax,cr0               ;将cr0送到eax
         or eax,1                  ;将最低位置为1
         mov cr0,eax                        ;设置PE位，处理器运行变为保护模式
      
         ;以下进入保护模式... ...
         jmp dword 0x0008:flush             ;16位的描述符选择子：32位偏移，此指令可以清空流水线，因为是跳转指令，处理器一般会清空流水线（清空段寄存器的高速缓存器，因为它在实模式下也使用了，里面的内容在保护模式可能出错，需尽快清除）。
                                            ;清流水线并串行化处理器（因为实模式下很多指令进入了流水线，那些指令可能导致出错，需尽快清除） 
         [bits 32]       ;伪指令，按32位模式进行译码。

    flush:
         mov cx,00000000000_10_000B         ;加载数据段选择子(0x10)，0位和1位是RPL（特权级），2位是TI（表示段在GDT（0）还是在LDT（1））
         mov ds,cx            ;把段选择子给cx，选择第二个段，即显示缓冲区段（0x0000b800）

         ;以下在屏幕上显示"Protect mode OK." 
         mov byte [0x00],'P'  
         mov byte [0x02],'r'
         mov byte [0x04],'o'
         mov byte [0x06],'t'
         mov byte [0x08],'e'
         mov byte [0x0a],'c'
         mov byte [0x0c],'t'
         mov byte [0x0e],' '
         mov byte [0x10],'m'
         mov byte [0x12],'o'
         mov byte [0x14],'d'
         mov byte [0x16],'e'
         mov byte [0x18],' '
         mov byte [0x1a],'O'
         mov byte [0x1c],'K'

         ;以下用简单的示例来帮助阐述32位保护模式下的堆栈操作 
         mov cx,00000000000_11_000B         ;加载堆栈段选择子
         mov ss,cx
         mov esp,0x7c00
          ;当前程序的代码段，其描述符的D位是“1”，所以，当进行隐式的栈操作时，默认地，每次压栈操作时，压入的是双字；当前程序所使用的栈段，其符述符的B 位也是“1”，默认地，使用栈指针寄存器ESP 进行操作。
         mov ebp,esp                        ;保存堆栈指针 
         push byte '.'                      ;压入立即数（字节），当前正在执行的代码段是32位，因为D位是1，所以压栈操作的操作数默认是32位，栈段B位也是1，使用ESP，push指令默认操作数是32位，byte告诉编译器，该指令对应的格式为push imm8，必须使用操作码0x6A，而不是用来在编译后的机器指令前添加指令前缀。因此，该指令实际在处理器上执行时，压入栈中的是一个双字，也就是4 字节，高24位是该字节符号的扩展。
         
         sub ebp,4
         cmp ebp,esp                        ;判断压入立即数时，ESP是否减4 
         jnz ghalt                          ;不是减4就直接跳转到ghalt
         pop eax
         mov [0x1e],al                      ;显示句点 
      
  ghalt:     
         hlt                                ;停机指令,已经禁止中断，将不会被唤醒 

;-------------------------------------------------------------------------------
     
         gdt_size         dw 0
         gdt_base         dd 0x00007e00     ;GDT的物理地址，从这个地方开始创建全局段描述符表（GDT） 
                             
         times 510-($-$$) db 0
                          db 0x55,0xaa