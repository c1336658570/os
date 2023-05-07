         ;代码清单8-2
         ;文件名：c08.asm
         ;文件说明：用户程序 
         ;创建日期：2011-5-5 18:17
         
;===============================================================================
SECTION header vstart=0                     ;定义用户程序头部段 
    program_length  dd program_end          ;程序总长度[0x00]
    
    ;用户程序入口点
    code_entry      dw start                ;偏移地址[0x04]，由于段声明处有vstart=0，所以start是相对于段开头的地址
                    dd section.code_1.start ;段地址[0x06],由于段起始地址可以是20位地址的任何地方，所以需要用dd保存
    
    realloc_tbl_len dw (header_end-code_1_segment)/4
                                            ;段重定位表项个数[0x0a],每个表项4字节
    
    ;段重定位表           
    code_1_segment  dd section.code_1.start ;[0x0c]     ;code_1段相对于程序开头的汇编地址
    code_2_segment  dd section.code_2.start ;[0x10]
    data_1_segment  dd section.data_1.start ;[0x14]
    data_2_segment  dd section.data_2.start ;[0x18]
    stack_segment   dd section.stack.start  ;[0x1c]
    
    header_end:                
    
;===============================================================================
SECTION code_1 align=16 vstart=0         ;定义代码段1（16字节对齐） ，vstart表示计算汇编地址时从该段开头开始计算，并且从0开始
put_string:                              ;显示串(0结尾)。
                                         ;输入：DS:BX=串地址
         mov cl,[bx]
         or cl,cl                        ;cl=0 ?,or会导致zf置位，如果cl为0则zf会为1
         jz .exit                        ;是的，返回主程序 
         call put_char
         inc bx                          ;下一个字符 
         jmp put_string

   .exit:
         ret

;-------------------------------------------------------------------------------
put_char:                                ;显示一个字符
                                         ;输入：cl=字符ascii
         push ax
         push bx
         push cx
         push dx
         push ds
         push es

         ;以下取当前光标位置
         mov dx,0x3d4           ;向0x3d4端口寄存器写入0x0e，然后从0x3d5中获取光标寄存器高8位
         mov al,0x0e
         out dx,al
         mov dx,0x3d5
         in al,dx                        ;高8位 
         mov ah,al

         mov dx,0x3d4           ;向0x3d4端口寄存器写入0x0f，然后从0x3d5中获取光标寄存器低8位
         mov al,0x0f
         out dx,al
         mov dx,0x3d5
         in al,dx                        ;低8位 
         mov bx,ax                       ;BX=代表光标位置的16位数

         cmp cl,0x0d                     ;回车符？
         jnz .put_0a                     ;不是。看看是不是换行等字符 
         mov ax,bx                       ;此句略显多余，但去掉后还得改书，麻烦 
         mov bl,80                       
         div bl
         mul bl                 ;ax = bl * al，在ax中得到当前行行首的光标值，早期窗口大小时25*80，从0->25*80-1即0->1999
         mov bx,ax
         jmp .set_cursor

 .put_0a:
         cmp cl,0x0a                     ;换行符？
         jnz .put_other                  ;不是，那就正常显示字符 
         add bx,80
         jmp .roll_screen
        ;屏幕可同时显示2000个字符。光标占1个字节，整个屏幕只有1个光标。一个字符在显存占俩字节
 .put_other:                             ;正常显示字符
         mov ax,0xb800
         mov es,ax
         shl bx,1          ;得到该位置字符在显存中的偏移地址
         mov [es:bx],cl

         ;以下将光标位置推进一个字符
         shr bx,1
         add bx,1
        ;屏幕只能显示25*80即2000个字符,0-1999，如果BX大于等于2000需要滚屏，即将2~25行向上提1行
 .roll_screen:
         cmp bx,2000                     ;光标超出屏幕？滚屏，即将2~25行内容整体向上提1行，用黑底白字空白字符填充25行。
         jl .set_cursor                 ;光标未超出屏幕，即bx小于2000

         mov ax,0xb800
         mov ds,ax
         mov es,ax
         cld
         mov si,0xa0            ;第2行第1列
         mov di,0x00
         mov cx,1920
         rep movsw
         mov bx,3840                     ;清除屏幕最底一行
         mov cx,80
 .cls:
         mov word[es:bx],0x0720         ;黑底白字空白字符
         add bx,2
         loop .cls

         mov bx,1920            ;光标的新位置

 .set_cursor:           ;新的光标位置写入光标寄存器
         mov dx,0x3d4
         mov al,0x0e      ;0x0e，       向0x3d4中写入0x0e，然后将bx高8位通过0x3d5写入
         out dx,al
         mov dx,0x3d5
         mov al,bh
         out dx,al
         mov dx,0x3d4
         mov al,0x0f      ;0x0f，       向0x3d4中写入0x0f,将bx低8位通过0x3d5写入
         out dx,al
         mov dx,0x3d5
         mov al,bl
         out dx,al

         pop es
         pop ds
         pop dx
         pop cx
         pop bx
         pop ax

         ret

;-------------------------------------------------------------------------------
  start:
         ;初始执行时，DS和ES指向用户程序头部段(在引导程序中设置的)
         mov ax,[stack_segment]           ;设置到用户程序自己的堆栈 
         mov ss,ax
         mov sp,stack_end  ;等价于mov sp, 256，因为stack_end段定义了vstart=0，所以stack_end汇编地址为256
         
         mov ax,[data_1_segment]          ;设置到用户程序自己的数据段
         mov ds,ax                      ;修改ds，从此处开始ds不再指向程序头部段

         mov bx,msg0
         call put_string                  ;显示第一段信息 

         push word [es:code_2_segment]  ;es一直指向用户程序头部段，这在加载程序中定义了从未更改过，压入code_2段地址
         mov ax,begin           ;begin是相对于code_2_segment段的偏移地址
         push ax                          ;可以直接push begin,80386+    压入偏移地址
         
         retf                             ;转移到代码段2执行 
         
  continue:
         mov ax,[es:data_2_segment]       ;段寄存器DS切换到数据段2 
         mov ds,ax
         
         mov bx,msg1
         call put_string                  ;显示第二段信息 

         jmp $ 

;===============================================================================
SECTION code_2 align=16 vstart=0          ;定义代码段2（16字节对齐）

  begin:
         push word [es:code_1_segment]  ;压入code_1的段地址
         mov ax,continue
         push ax                          ;可以直接push continue,80386+   压入continue的汇编地址
         
         retf                             ;转移到代码段1接着执行 
         
;===============================================================================
SECTION data_1 align=16 vstart=0

    msg0 db '  This is NASM - the famous Netwide Assembler. '
         db 'Back at SourceForge and in intensive development! '
         db 'Get the current versions from http://www.nasm.us/.'
         db 0x0d,0x0a,0x0d,0x0a
         db '  Example code for calculate 1+2+...+1000:',0x0d,0x0a,0x0d,0x0a    ;0x0d回车，0x0a换行
         db '     xor dx,dx',0x0d,0x0a
         db '     xor ax,ax',0x0d,0x0a
         db '     xor cx,cx',0x0d,0x0a
         db '  @@:',0x0d,0x0a
         db '     inc cx',0x0d,0x0a
         db '     add ax,cx',0x0d,0x0a
         db '     adc dx,0',0x0d,0x0a
         db '     inc cx',0x0d,0x0a
         db '     cmp cx,1000',0x0d,0x0a
         db '     jle @@',0x0d,0x0a
         db '     ... ...(Some other codes)',0x0d,0x0a,0x0d,0x0a
         db 0           ;终止字符串

;===============================================================================
SECTION data_2 align=16 vstart=0

    msg1 db '  The above contents is written by LeeChung. '
         db '2011-05-06'
         db 0

;===============================================================================
SECTION stack align=16 vstart=0
           
         resb 256       ;伪指令resb用来保留256字节的栈空间。

stack_end:  ;此处汇编地址为256

;===============================================================================
SECTION trail align=16          ;没有vstart，表示计算汇编地址从程序开头计算
program_end:            ;表示程序的大小