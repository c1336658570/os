         ;代码清单15-2
         ;文件名：c15.asm
         ;文件说明：用户程序 
         ;创建日期：2011-11-15 19:11   

;===============================================================================
SECTION header vstart=0

         program_length   dd program_end          ;程序总长度#0x00
         
         head_len         dd header_end           ;程序头部的长度#0x04

         stack_seg        dd 0                    ;用于接收堆栈段选择子#0x08
         stack_len        dd 1                    ;程序建议的堆栈大小#0x0c
                                                  ;以4KB为单位
                                                  
         prgentry         dd start                ;程序入口#0x10 
         code_seg         dd section.code.start   ;代码段位置#0x14
         code_len         dd code_end             ;代码段长度#0x18

         data_seg         dd section.data.start   ;数据段位置#0x1c
         data_len         dd data_end             ;数据段长度#0x20
;-------------------------------------------------------------------------------
         ;符号地址检索表
         salt_items       dd (header_end-salt)/256 ;#0x24
         
         salt:                                     ;#0x28
         PrintString      db  '@PrintString'
                     times 256-($-PrintString) db 0
                     
         TerminateProgram db  '@TerminateProgram'
                     times 256-($-TerminateProgram) db 0
                     
         ReadDiskData     db  '@ReadDiskData'
                     times 256-($-ReadDiskData) db 0
                 
header_end:
  
;===============================================================================
SECTION data vstart=0                

         message_1        db  0x0d,0x0a
                          db  '[USER TASK]: Hi! nice to meet you,'
                          db  'I am run at CPL=',0
                          
         message_2        db  0
                          db  '.Now,I must exit...',0x0d,0x0a,0

data_end:

;===============================================================================
      [bits 32]
;===============================================================================
SECTION code vstart=0
start:
         ;任务启动时，DS指向头部段，也不需要设置堆栈 
         mov eax,ds
         mov fs,eax     ;令段寄存器FS指向头部段。其主要目的是保存指向头部段的指针以备后用，同时，腾出段寄存器DS来完成后续操作
;令段寄存器DS指向当前任务自己的数据段。
         mov eax,[data_seg]
         mov ds,eax
;显示一些信息
         mov ebx,message_1
         call far [fs:PrintString]
         
         mov ax,cs
         and al,0000_0011B    ;数字3
         or al,0x0030         ;等于+30，将3转为ASCII的3。
         mov [message_2],al   ;然后将其写入到message_2的第一个字节
;显示message_2的一些信息
         mov ebx,message_2
         call far [fs:PrintString]
;第74行，通过调用门转到全局空间执行。
         call far [fs:TerminateProgram]      ;退出，并将控制权返回到核心 跳到内核的354行
    
code_end:

;-------------------------------------------------------------------------------
SECTION trail
;-------------------------------------------------------------------------------
program_end: