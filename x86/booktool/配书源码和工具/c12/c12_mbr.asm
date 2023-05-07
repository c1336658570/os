         ;代码清单12-1
         ;文件名：c12_mbr.asm
         ;文件说明：硬盘主引导扇区代码 
         ;创建日期：2011-10-27 22:52

         ;设置堆栈段和栈指针 
         mov eax,cs      ;cs:0x0000，ip:7c00
         mov ss,eax
         mov sp,0x7c00
      
         ;计算GDT所在的逻辑段地址
         mov eax,[cs:pgdt+0x7c00+0x02]      ;GDT的32位线性基地址 
         xor edx,edx
         mov ebx,16
         div ebx                            ;分解成16位逻辑地址 
       ;eax和edx仅低地址有效，即dx和ax
         mov ds,eax                         ;令DS指向该段以进行操作
         mov ebx,edx                        ;段内起始偏移地址 

         ;创建0#描述符，它是空描述符，这是处理器的要求
         mov dword [ebx+0x00],0x00000000
         mov dword [ebx+0x04],0x00000000  

         ;创建1#描述符，这是一个数据段，对应0~4GB的线性地址空间
         mov dword [ebx+0x08],0x0000ffff    ;基地址为0，段界限为0xfffff，32位，数据段，特权级00
         mov dword [ebx+0x0c],0x00cf9200    ;粒度为4KB（段界限以4K为单位），存储器段描述符 

         ;创建保护模式下初始代码段描述符
         mov dword [ebx+0x10],0x7c0001ff    ;基地址为0x00007c00，段界限为0x001ff，512字节，32位，代码段，特权级00 
         mov dword [ebx+0x14],0x00409800    ;粒度为1个字节，代码段描述符 

         ;创建以上代码段的别名描述符
         mov dword [ebx+0x18],0x7c0001ff    ;基地址为0x00007c00，段界限为0x001FF，512字节，32位，数据段，特权级00
         mov dword [ebx+0x1c],0x00409200    ;粒度为1个字节，数据段描述符  因为保护模式下代码段不能写入，只执行或执行和读，所以有该段
              ;操作该段处理器的检查规则是：0xFFFFF000（0xFFFFEFFF+1） <= (ESP - 操作数长度) <= 0xFFFFFFFF	;0xFFFFEFFF+1 = 0XFFFFF000
         mov dword [ebx+0x20],0x7c00fffe    ;基地址0x00007c00，界限0xFFFFE（实际段界限：0xFFFFE * 0X1000 + 0XFFF = 0xFFFFEFFF），32位
         mov dword [ebx+0x24],0x00cf9600    ;4k粒度，栈段，特权级00     
         
         ;初始化描述符表寄存器GDTR
         mov word [cs: pgdt+0x7c00],39      ;描述符表的界限(5个段描述符一个8字节，总字节数减1)   
 
         lgdt [cs: pgdt+0x7c00]    ;设置gdt寄存器，将该内存的内容放到gdt中，操作数是6字节，低16位是GDT界限值(39)，高32位是GDT基地址(0x00007e00)。
       ;通过访问南桥芯片端口，可以启用A20地址线，以前的地址线只有20根，当地址增加到0xFFFFF后+1会回到0x00000，切换到32位保护模式后，0xFFFFF加1会产生进位，所以要打开A20地址线
         in al,0x92                         ;南桥芯片内的端口 
         or al,0000_0010B
         out 0x92,al                        ;打开A20

         cli                                ;中断机制尚未工作

         mov eax,cr0
         or eax,1
         mov cr0,eax                        ;设置PE位
      
         ;以下进入保护模式... ...
         jmp dword 0x0010:flush             ;16位的描述符选择子：32位偏移。CS指向512 字节的32位代码段，基地址是0x00007C00
                                             
         [bits 32]                          
  flush:                                     
         mov eax,0x0018                   ;0000_0000_0001_1000 0位和1位是RPL（特权级），2位是TI（表示段在GDT（0）还是在LDT（1）） 
         mov ds,eax  ;DS 指向512 字节的32 位数据段，该段是上述代码段的别名，因此基地址也是0x00007C00
      
         mov eax,0x0008                     ;0000_0000_0000_1000加载数据段(0..4GB)选择子
         mov es,eax         ;ES、FS 和GS 指向同一个段，该段是一个4GB 的32 位数据段，基地址为0x00000000
         mov fs,eax
         mov gs,eax
      
         mov eax,0x0020                     ;0000 0000 0010 0000
         mov ss,eax         ;SS 指向4KB 的32 位栈段，基地址为0x00007C00。
         xor esp,esp                        ;ESP <- 0
      
         mov dword [es:0x0b8000],0x072e0750 ;字符'P'、'.'及其显示属性
         mov dword [es:0x0b8004],0x072e074d ;字符'M'、'.'及其显示属性
         mov dword [es:0x0b8008],0x07200720 ;两个空白字符及其显示属性
         mov dword [es:0x0b800c],0x076b076f ;字符'o'、'k'及其显示属性

         ;开始冒泡排序 
         mov ecx,pgdt-string-1              ;遍历次数=串长度-1 
  @@1:
         push ecx                           ;32位模式下的loop使用ecx 
         xor bx,bx                          ;32位模式下，偏移量可以是16位，也可以，此处未用ebx为了让你知道32位也可用16位寄存器寻址 
  @@2:                                      ;是后面的32位 
         mov ax,[string+bx] 
         cmp ah,al                          ;ah中存放的是源字的高字节 
         jge @@3 
         xchg al,ah  ;xchg 是交换指令，用于交换两个操作数的内容
         mov [string+bx],ax 
  @@3:
         inc bx 
         loop @@2 
         pop ecx 
         loop @@1
       ;用于显示排序结果
         mov ecx,pgdt-string
         xor ebx,ebx                        ;偏移地址是32位的情况 
  @@4:                                      ;32位的偏移具有更大的灵活性
         mov ah,0x07
         mov al,[string+ebx]
         mov [es:0xb80a0+ebx*2],ax          ;演示0~4GB寻址。0xb80a0 等于0xb8000 加上十进制数160（0xa0）。在显存中，偏移量为160 的地方对应着屏幕第2 行第1 列。
         inc ebx
         loop @@4
      
         hlt 

;-------------------------------------------------------------------------------
     string           db 's0ke4or92xap3fv8giuzjcy5l1m7hd6bnqtw.'
;-------------------------------------------------------------------------------
     pgdt             dw 0          ;gdt_size
                      dd 0x00007e00      ;GDT的物理地址 gdt_base
;-------------------------------------------------------------------------------                             
     times 510-($-$$) db 0
                      db 0x55,0xaa