         ;代码清单13-1
         ;文件名：c13_mbr.asm
         ;文件说明：硬盘主引导扇区代码 
         ;创建日期：2011-10-28 22:35        ;设置堆栈段和栈指针 
         
         core_base_address equ 0x00040000   ;常数，内核加载的起始内存地址 
         core_start_sector equ 0x00000001   ;常数，内核的起始逻辑扇区号 
         
         mov ax,cs      
         mov ss,ax
         mov sp,0x7c00
      
         ;计算GDT所在的逻辑段地址
         mov eax,[cs:pgdt+0x7c00+0x02]      ;GDT的32位物理地址 
         xor edx,edx
         mov ebx,16
         div ebx                            ;分解成16位逻辑地址 

         mov ds,eax                         ;令DS指向该段以进行操作
         mov ebx,edx                        ;段内起始偏移地址 

         ;跳过0#号描述符的槽位 
         ;创建1#描述符，这是一个数据段，对应0~4GB的线性地址空间
         mov dword [ebx+0x08],0x0000ffff    ;基地址为0，段界限为0xFFFFF
         mov dword [ebx+0x0c],0x00cf9200    ;粒度为4KB，存储器段描述符 

         ;创建保护模式下初始代码段描述符
         mov dword [ebx+0x10],0x7c0001ff    ;基地址为0x00007c00，界限0x1FF 
         mov dword [ebx+0x14],0x00409800    ;粒度为1个字节，代码段描述符 

         ;建立保护模式下的堆栈段描述符      ;基地址为0x00007C00，界限0xFFFFE 
         mov dword [ebx+0x18],0x7c00fffe    ;粒度为4KB 
         mov dword [ebx+0x1c],0x00cf9600
         
         ;建立保护模式下的显示缓冲区描述符   
         mov dword [ebx+0x20],0x80007fff    ;基地址为0x000B8000，界限0x07FFF 
         mov dword [ebx+0x24],0x0040920b    ;粒度为字节
         
         ;初始化描述符表寄存器GDTR
         mov word [cs: pgdt+0x7c00],39      ;描述符表的界限   
 
         lgdt [cs: pgdt+0x7c00]
      
         in al,0x92                         ;南桥芯片内的端口 
         or al,0000_0010B          ;通过设置该位，打开第21根地址线
         out 0x92,al                        ;打开A20

         cli                                ;中断机制尚未工作

         mov eax,cr0
         or eax,1
         mov cr0,eax                        ;设置PE位
      
         ;以下进入保护模式... ...
         jmp dword 0x0010:flush             ;16位的描述符选择子：32位偏移
                                            ;清流水线并串行化处理器
         [bits 32]               
  flush:                                  
         mov eax,0x0008                     ;加载数据段(0..4GB)选择子
         mov ds,eax
      
         mov eax,0x0018                     ;加载堆栈段选择子 
         mov ss,eax
         xor esp,esp                        ;堆栈指针 <- 0 
         
         ;以下加载系统核心程序 
         mov edi,core_base_address        ;内核加载的地址
       ;eax:ebx      使用EAX寄存器来传入28位的逻辑扇区号，使用EBX寄存器来传入偏移地址
         mov eax,core_start_sector        ;内核所在扇区的地址
         mov ebx,edi                        ;起始地址 
         call read_hard_disk_0              ;以下读取程序的起始部分（一个扇区） 
       ;调用read_hard_disk_0结束时，每次将EBX寄存器的内容加上512，目的是指向下一个内存块
         ;以下判断整个程序有多大
         mov eax,[edi]                      ;核心程序尺寸
         xor edx,edx 
         mov ecx,512                        ;512字节每扇区
         div ecx

         or edx,edx                       ;or会影响ZF位
         jnz @1                             ;未除尽，因此结果比实际扇区数少1 
         dec eax                            ;已经读了一个扇区，扇区总数减1 
   @1:
         or eax,eax                         ;考虑实际长度≤512个字节的情况    检查eax是否为，or会影响ZF位
         jz setup                           ;EAX=0 ?

         ;读取剩余的扇区
         mov ecx,eax                        ;32位模式下的LOOP使用ECX
         mov eax,core_start_sector
         inc eax                            ;从下一个逻辑扇区接着读
   @2:
         call read_hard_disk_0
         inc eax
         loop @2                            ;循环读，直到读完整个内核 

 setup:   ;保护模式下的代码段只是用来执行的，是否能读出，取决于其描述符的类别字段，但无论如何它都不能写入。
         mov esi,[0x7c00+pgdt+0x02]         ;不可以在代码段内寻址pgdt，因为处于保护模式，但可以
                                            ;通过4GB的数据段来访问
         ;建立公用例程段描述符
         mov eax,[edi+0x04]                 ;公用例程代码段起始汇编地址
         mov ebx,[edi+0x08]                 ;核心数据段汇编地址
         sub ebx,eax ;用内核数据段的起始汇编地址-公共例程段的起始汇编地址-1，就是公共例程段的段界限
         dec ebx                            ;公用例程段界限 
         add eax,edi                        ;公用例程段基地址  公共例程段的起始汇编地址+内核的加载地址
         mov ecx,0x00409800                 ;字节粒度的代码段描述符   该段的一个属性
         call make_gdt_descriptor  ;创建段描述符
         mov [esi+0x28],eax        ;访问4GB的段，定位到GDT，在原先的基础上，再添加一个描述符
         mov [esi+0x2c],edx
       
         ;建立核心数据段描述符
         mov eax,[edi+0x08]                 ;核心数据段起始汇编地址
         mov ebx,[edi+0x0c]                 ;核心代码段汇编地址 
         sub ebx,eax ;用核心代码段汇编地址-核心数据段起始汇编地址-1，就是核心数据段的段界限
         dec ebx                            ;核心数据段界限
         add eax,edi                        ;核心数据段基地址  核心数据段起始汇编地址+内核的加载地址
         mov ecx,0x00409200                 ;字节粒度的数据段描述符   该段的一个属性
         call make_gdt_descriptor  ;创建段描述符
         mov [esi+0x30],eax        ;访问4GB的段，定位到GDT，在原先的基础上，再添加一个描述符
         mov [esi+0x34],edx 
      
         ;建立核心代码段描述符
         mov eax,[edi+0x0c]                 ;核心代码段起始汇编地址
         mov ebx,[edi+0x00]                 ;程序总长度
         sub ebx,eax ;用程序总长度-核心代码段起始汇编地址-1，就是核心代码段的段界限
         dec ebx                            ;核心代码段界限
         add eax,edi                        ;核心代码段基地址  核心代码段起始汇编地址+内核的加载地址
         mov ecx,0x00409800                 ;字节粒度的代码段描述符   该段的一个属性
         call make_gdt_descriptor  ;创建段描述符
         mov [esi+0x38],eax        ;访问4GB的段，定位到GDT，在原先的基础上，再添加一个描述符
         mov [esi+0x3c],edx

         mov word [0x7c00+pgdt],63          ;描述符表的界限    共有8个段描述符
                                        
         lgdt [0x7c00+pgdt]                  

         jmp far [edi+0x10]        ;通过4GB的数据段访问内核的头部，用间接远转移指令从给定的入口进入内核执行
       
;-------------------------------------------------------------------------------
read_hard_disk_0:                        ;从硬盘读取一个逻辑扇区
                                         ;EAX=逻辑扇区号
                                         ;DS:EBX=目标缓冲区地址
                                         ;返回：EBX=EBX+512 
         push eax 
         push ecx
         push edx
      
         push eax
         
         mov dx,0x1f2
         mov al,1
         out dx,al                       ;读取的扇区数

         inc dx                          ;0x1f3
         pop eax
         out dx,al                       ;LBA地址7~0

         inc dx                          ;0x1f4
         mov cl,8
         shr eax,cl
         out dx,al                       ;LBA地址15~8

         inc dx                          ;0x1f5
         shr eax,cl
         out dx,al                       ;LBA地址23~16

         inc dx                          ;0x1f6
         shr eax,cl
         or al,0xe0                      ;第一硬盘  LBA地址27~24      采用LBA模式，主盘
         out dx,al

         inc dx                          ;0x1f7
         mov al,0x20                     ;读命令
         out dx,al

  .waits:
         in al,dx
         and al,0x88
         cmp al,0x08
         jnz .waits                      ;不忙，且硬盘已准备好数据传输 

         mov ecx,256                     ;总共要读取的字数
         mov dx,0x1f0
  .readw:
         in ax,dx
         mov [ebx],ax
         add ebx,2
         loop .readw

         pop edx
         pop ecx
         pop eax
      
         ret

;-------------------------------------------------------------------------------
make_gdt_descriptor:                     ;构造描述符
                                         ;输入：EAX=线性基地址
                                         ;      EBX=段界限
                                         ;      ECX=属性（各属性位都在原始
                                         ;      位置，其它没用到的位置0） 
                                         ;返回：EDX:EAX=完整的描述符
         mov edx,eax ;201->203行构造描述符的低32位。高16位为段基地址的15-0位，低16位为段界限的15-0位
         shl eax,16                     
         or ax,bx                        ;描述符前32位(EAX)构造完毕
;段描述符高32位布局，31-24为段基址的31-24，23-20为权限，19-16为段界限的19-16位，15-8为权限，7-0为段基地址的23-16位。
         and edx,0xffff0000              ;清除基地址中无关的位   清除基地址低16位，因为基地址低16位已经构造过了
         rol edx,8   ;循环左移8位，现在31-24位为段基地址的23-16位，7-0位为段基地址的31-24位，段描述符的31-24为段基址的31-24，7-0为段基地址的23-16位，与此正好相反
         bswap edx                       ;装配基址的31~24和23~16  (80486+)   交换第1字节和第4字节，交换第2字节和第3字节
      
         xor bx,bx   ;这里是假设EBX寄存器的高12位为全零，所以用了xor bx，bx指令。实际上，安全的做法是使用指令and ebx,0x000f0000
         or edx,ebx                      ;装配段界限的高4位
      
         or edx,ecx                      ;装配属性 
      
         ret
      
;-------------------------------------------------------------------------------
         pgdt             dw 0
                          dd 0x00007e00      ;GDT的物理地址
;-------------------------------------------------------------------------------                             
         times 510-($-$$) db 0
                          db 0x55,0xaa