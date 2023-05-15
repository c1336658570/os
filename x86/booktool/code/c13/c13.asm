         ;代码清单13-3
         ;文件名：c13.asm
         ;文件说明：用户程序 
         ;创建日期：2011-10-30 15:19   
         
;===============================================================================
SECTION header vstart=0

         program_length   dd program_end          ;程序总长度#0x00
         
         head_len         dd header_end           ;程序头部的长度#0x04  重定位后此处存的是用户程序头部段的段选择子
      ;内核不要求用户程序提供栈空间，而改由内核动态分配，以减轻用户程序编写的负担。当内核分配了栈空间后，会把栈段的选择子填写到这里
         stack_seg        dd 0                    ;用于接收堆栈段选择子#0x08
         stack_len        dd 1                    ;程序建议的堆栈大小#0x0c
                                                  ;以4KB为单位    如果是1，就是希望分配4KB的栈空间
                                                  
         prgentry         dd start                ;程序入口#0x10  32位偏移地址。
         code_seg         dd section.code.start   ;代码段位置#0x14      用户程序代码段的起始汇编地址。当内核完成对用户程序的加载和重定位后，将把该段的选择子回填到这里（仅占用低字部分）
         code_len         dd code_end             ;代码段长度#0x18

         data_seg         dd section.data.start   ;数据段位置#0x1c      是用户程序数据段的起始汇编地址，当内核完成用户程序的加载和重定位后，将把该段的选择子回填到这里（仅占用低字部分）
         data_len         dd data_end             ;数据段长度#0x20
;-------------------------------------------------------------------------------
;内核还提供一些例程供用户程序调用。内核要求，用户程序必须在头部偏移量为0x28的地方构造一个表格，并在表格中列出所有要用到的符号名。每个符号名的长度是256字节，不足部分用0x00填充。在用户程序加载后，内核会分析这个表格，并将每一个符号名替换成相应的内存地址，这就是过程的重定位。
         ;符号地址检索表
         salt_items       dd (header_end-salt)/256 ;#0x24   符号名的数量
;初始化了三个符号名，每一个256 字节，不足部分是用0填充的。每个符号名都以“@”开始，这并没有任何特殊意义，仅仅在概念上用于表示“接口”的意思。
         salt:                                     ;#0x28
         PrintString      db  '@PrintString'
                     times 256-($-PrintString) db 0   ;填充0让符号名达到256个字节
                     
         TerminateProgram db  '@TerminateProgram'
                     times 256-($-TerminateProgram) db 0
                     
         ReadDiskData     db  '@ReadDiskData'
                     times 256-($-ReadDiskData) db 0
                 
header_end:

;===============================================================================
SECTION data vstart=0    
                         
         buffer times 1024 db  0         ;缓冲区

         message_1         db  0x0d,0x0a,0x0d,0x0a
                           db  '**********User program is runing**********'
                           db  0x0d,0x0a,0
         message_2         db  '  Disk data:',0x0d,0x0a,0

data_end:

;===============================================================================
      [bits 32]
;===============================================================================
SECTION code vstart=0
start:
         mov eax,ds     ;ds指向用户程序头部，将其传送到eax中
         mov fs,eax     ;让fs保存用户程序头部
      ;切换到用户程序自己的栈，并初始化栈指针寄存器ESP的内容为0。
         mov eax,[stack_seg]
         mov ss,eax
         mov esp,0
      ;设置段寄存器DS到用户程序自己的数据段。
         mov eax,[data_seg]
         mov ds,eax
      ;调用内核过程显示字符串，以表明用户程序正在运行中。该内核过程要求用DS:EBX指向零终止的字符串。
         mov ebx,message_1
         call far [fs:PrintString]
      ;调用内核过程，从硬盘读一个扇区，第一个是EAX寄存器，传入要读的逻辑扇区号；第二个是DS:EBX，传入缓冲区的首地址
         mov eax,100                         ;逻辑扇区号100
         mov ebx,buffer                      ;缓冲区偏移地址
         call far [fs:ReadDiskData]          ;段间调用
      ;调用内核过程显示一个题头
         mov ebx,message_2
         call far [fs:PrintString]
      ;再次调用内核过程显示刚刚从硬盘读出的内容。
         mov ebx,buffer 
         call far [fs:PrintString]           ;too.
      ;调用内核过程，以返回到内核。
         jmp far [fs:TerminateProgram]       ;将控制权返回到系统 
      
code_end:

;===============================================================================
SECTION trail
;-------------------------------------------------------------------------------
program_end: