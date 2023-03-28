         ;代码清单8-1
         ;文件名：c08_mbr.asm
         ;文件说明：硬盘主引导扇区代码（加载程序） 
         ;创建日期：2011-5-5 18:17
         
         app_lba_start equ 100           ;声明常数（用户程序起始逻辑扇区号）
                                         ;常数的声明不会占用汇编地址
                                    
SECTION mbr align=16 vstart=0x7c00       ;主引导扇区定义为1个段,加上vstart=0x7c00表示段内所有元素汇编地址从0x7c00开始计算                              

         ;设置堆栈段和栈指针 
         mov ax,0      
         mov ss,ax
         mov sp,ax      ;栈指针sp在段内0xFFFF-0x0000之间变化
      
         mov ax,[cs:phy_base]            ;计算用于加载用户程序的逻辑段地址 
         mov dx,[cs:phy_base+0x02]       ;不需要加0x7c00，因为定义段时定义了vstart=0x7c00
         mov bx,16        
         div bx                          ;除16是因为计算用户程序的段地址
         mov ds,ax                       ;令DS和ES指向该段以进行操作
         mov es,ax                        
    
         ;以下读取程序的起始部分 
         xor di,di                       ;逻辑扇区号的高12位
         mov si,app_lba_start            ;程序在硬盘上的起始逻辑扇区号，逻辑扇区号低16位 
         xor bx,bx                       ;加载到DS:0x0000处 
         call read_hard_disk_0
      
         ;以下判断整个程序有多大
         mov dx,[2]                      ;曾经把dx写成了ds，花了二十分钟排错 
         mov ax,[0]
         mov bx,512                      ;512字节每扇区
         div bx
         cmp dx,0
         jnz @1                          ;未除尽，因此结果比实际扇区数少1 
         dec ax                          ;已经读了一个扇区，扇区总数减1 
   @1:
         cmp ax,0                        ;考虑实际长度小于等于512个字节的情况 
         jz direct
         
         ;读取剩余的扇区
         push ds                         ;以下要用到并改变DS寄存器（重新构造逻辑段），一个逻辑段最大64K，当用户程序特别大时一个逻辑段存不下，bx从0x0000到0xffff用完之后，又会重新回到0x0000覆盖掉以前的内容。 

         mov cx,ax                       ;循环次数（剩余扇区数）
   @2:
         mov ax,ds
         add ax,0x20                     ;得到下一个以512字节为边界的段地址
         mov ds,ax  
                              
         xor bx,bx                       ;每次读时，偏移地址始终为0x0000 
         inc si                          ;下一个逻辑扇区 
         call read_hard_disk_0           ;
         loop @2                         ;循环读，直到读完整个功能程序 

         pop ds                          ;恢复数据段基址到用户程序头部段 
      
         ;计算入口点代码段基址 
   direct:
         mov dx,[0x08]
         mov ax,[0x06]
         call calc_segment_base
         mov [0x06],ax                   ;回填修正后的入口点代码段基址 
      
         ;开始处理段重定位表
         mov cx,[0x0a]                   ;需要重定位的项目数量
         mov bx,0x0c                     ;重定位表首地址
          
 realloc:
         mov dx,[bx+0x02]                ;32位地址的高16位 
         mov ax,[bx]
         call calc_segment_base
         mov [bx],ax                     ;回填段的基址
         add bx,4                        ;下一个重定位项（每项占4个字节） 
         loop realloc 
      
         jmp far [0x04]                  ;转移到用户程序  
 
;-------------------------------------------------------------------------------
read_hard_disk_0:                        ;从硬盘读取一个逻辑扇区
                                         ;输入：DI:SI=起始逻辑扇区号,DI中只有低12位有效，高4位置0，因为逻辑扇区号是28位
                                         ;      DS:BX=目标缓冲区地址
         push ax
         push bx
         push cx
         push dx
         ;向0x1f端口写入要读取的扇区数
         mov dx,0x1f2         ;向0x1f2端口写入要读取的扇区数量
         mov al,1
         out dx,al                       ;读取的扇区数
         ;向硬盘接口写入起始逻辑扇区号的低24位
         inc dx                          ;0x1f3
         mov ax,si
         out dx,al                       ;LBA地址7~0

         inc dx                          ;0x1f4
         mov al,ah
         out dx,al                       ;LBA地址15~8

         inc dx                          ;0x1f5
         mov ax,di
         out dx,al                       ;LBA地址23~16
         ;向硬盘接口写入27~24位和工作模式（LBA）
         inc dx                          ;0x1f6
         mov al,0xe0                     ;LBA28模式，主盘
         or al,ah                        ;LBA地址27~24
         out dx,al
         ;向硬盘接口发送读命令
         inc dx                          ;0x1f7
         mov al,0x20                     ;读命令
         out dx,al

  .waits:
         in al,dx
         and al,0x88
         cmp al,0x08
         jnz .waits                      ;不忙，且硬盘已准备好数据传输 
         
         mov cx,256                      ;总共要读取的字数
         mov dx,0x1f0         ;0x1f0为16位的端口
  .readw:
         in ax,dx
         mov [bx],ax          ;将读出来的数据放到ds指向的数据段，起始偏移地址在bx中。
         add bx,2
         loop .readw

         pop dx
         pop cx
         pop bx
         pop ax
      
         ret

;-------------------------------------------------------------------------------
calc_segment_base:                       ;计算16位段地址
                                         ;输入：DX:AX=32位物理地址
                                         ;返回：AX=16位段基地址 
         push dx                          
         
         add ax,[cs:phy_base]           ;不需要加0x7c00，因为定义段时定义了vstart=0x7c00
         adc dx,[cs:phy_base+0x02]      ;adc是带进位加法，将目的操作数和源操作数相加，再加上标志寄存器CF的值
         shr ax,4                       ;逻辑右移，将ax右移4位，最低4位丢弃，最高4位补0
         ror dx,4                       ;循环右移,将最低4位移动到最高4位
         and dx,0xf000                  ;清空dx低12位
         or ax,dx
         
         pop dx
         
         ret

;-------------------------------------------------------------------------------
         phy_base dd 0x10000             ;用户程序被加载的物理起始地址，必须16字节对齐，用32位存地址，因为地址线有20位
         
 times 510-($-$$) db 0
                  db 0x55,0xaa