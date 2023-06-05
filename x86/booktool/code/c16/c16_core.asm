         ;代码清单16-1
         ;文件名：c16_core.asm
         ;文件说明：保护模式微型核心程序 
         ;创建日期：2012-06-20 00:05

         ;以下常量定义部分。内核的大部分内容都应当固定
         core_code_seg_sel     equ  0x38    ;内核代码段选择子
         core_data_seg_sel     equ  0x30    ;内核数据段选择子 
         sys_routine_seg_sel   equ  0x28    ;系统公共例程代码段的选择子 
         video_ram_seg_sel     equ  0x20    ;视频显示缓冲区的段选择子
         core_stack_seg_sel    equ  0x18    ;内核堆栈段选择子
         mem_0_4_gb_seg_sel    equ  0x08    ;整个0-4GB内存的段的选择子

;-------------------------------------------------------------------------------
         ;以下是系统核心的头部，用于加载核心程序 
         core_length      dd core_end       ;核心程序总长度#00

         sys_routine_seg  dd section.sys_routine.start
                                            ;系统公用例程段位置#04

         core_data_seg    dd section.core_data.start
                                            ;核心数据段位置#08

         core_code_seg    dd section.core_code.start
                                            ;核心代码段位置#0c


         core_entry       dd start          ;核心代码段入口点#10
                          dw core_code_seg_sel

;===============================================================================
         [bits 32]
;===============================================================================
SECTION sys_routine vstart=0                ;系统公共例程代码段 
;-------------------------------------------------------------------------------
         ;字符串显示例程
put_string:                                 ;显示0终止的字符串并移动光标 
                                            ;输入：DS:EBX=串地址
         push ecx
  .getc:
         mov cl,[ebx]
         or cl,cl
         jz .exit
         call put_char
         inc ebx
         jmp .getc

  .exit:
         pop ecx
         retf                               ;段间返回

;-------------------------------------------------------------------------------
put_char:                                   ;在当前光标处显示一个字符,并推进
                                            ;光标。仅用于段内调用 
                                            ;输入：CL=字符ASCII码 
         pushad

         ;以下取当前光标位置
         mov dx,0x3d4
         mov al,0x0e
         out dx,al
         inc dx                             ;0x3d5
         in al,dx                           ;高字
         mov ah,al

         dec dx                             ;0x3d4
         mov al,0x0f
         out dx,al
         inc dx                             ;0x3d5
         in al,dx                           ;低字
         mov bx,ax                          ;BX=代表光标位置的16位数

         cmp cl,0x0d                        ;回车符？
         jnz .put_0a
         mov ax,bx
         mov bl,80
         div bl
         mul bl
         mov bx,ax
         jmp .set_cursor

  .put_0a:
         cmp cl,0x0a                        ;换行符？
         jnz .put_other
         add bx,80
         jmp .roll_screen

  .put_other:                               ;正常显示字符
         push es
         mov eax,video_ram_seg_sel          ;0x800b8000段的选择子
         mov es,eax
         shl bx,1
         mov [es:bx],cl
         pop es

         ;以下将光标位置推进一个字符
         shr bx,1
         inc bx

  .roll_screen:
         cmp bx,2000                        ;光标超出屏幕？滚屏
         jl .set_cursor

         push ds
         push es
         mov eax,video_ram_seg_sel
         mov ds,eax
         mov es,eax
         cld
         mov esi,0xa0                       ;小心！32位模式下movsb/w/d 
         mov edi,0x00                       ;使用的是esi/edi/ecx 
         mov ecx,1920
         rep movsd
         mov bx,3840                        ;清除屏幕最底一行
         mov ecx,80                         ;32位程序应该使用ECX
  .cls:
         mov word[es:bx],0x0720
         add bx,2
         loop .cls

         pop es
         pop ds

         mov bx,1920

  .set_cursor:
         mov dx,0x3d4
         mov al,0x0e
         out dx,al
         inc dx                             ;0x3d5
         mov al,bh
         out dx,al
         dec dx                             ;0x3d4
         mov al,0x0f
         out dx,al
         inc dx                             ;0x3d5
         mov al,bl
         out dx,al

         popad
         
         ret                                

;-------------------------------------------------------------------------------
read_hard_disk_0:                           ;从硬盘读取一个逻辑扇区
                                            ;EAX=逻辑扇区号
                                            ;DS:EBX=目标缓冲区地址
                                            ;返回：EBX=EBX+512
         push eax 
         push ecx
         push edx
      
         push eax
         
         mov dx,0x1f2
         mov al,1
         out dx,al                          ;读取的扇区数

         inc dx                             ;0x1f3
         pop eax
         out dx,al                          ;LBA地址7~0

         inc dx                             ;0x1f4
         mov cl,8
         shr eax,cl
         out dx,al                          ;LBA地址15~8

         inc dx                             ;0x1f5
         shr eax,cl
         out dx,al                          ;LBA地址23~16

         inc dx                             ;0x1f6
         shr eax,cl
         or al,0xe0                         ;第一硬盘  LBA地址27~24
         out dx,al

         inc dx                             ;0x1f7
         mov al,0x20                        ;读命令
         out dx,al

  .waits:
         in al,dx
         and al,0x88
         cmp al,0x08
         jnz .waits                         ;不忙，且硬盘已准备好数据传输 

         mov ecx,256                        ;总共要读取的字数
         mov dx,0x1f0
  .readw:
         in ax,dx
         mov [ebx],ax
         add ebx,2
         loop .readw

         pop edx
         pop ecx
         pop eax
      
         retf                               ;段间返回 

;-------------------------------------------------------------------------------
;汇编语言程序是极难一次成功，而且调试非常困难。这个例程可以提供帮助 
put_hex_dword:                              ;在当前光标处以十六进制形式显示
                                            ;一个双字并推进光标 
                                            ;输入：EDX=要转换并显示的数字
                                            ;输出：无
         pushad
         push ds
      
         mov ax,core_data_seg_sel           ;切换到核心数据段 
         mov ds,ax
      
         mov ebx,bin_hex                    ;指向核心数据段内的转换表
         mov ecx,8
  .xlt:    
         rol edx,4
         mov eax,edx
         and eax,0x0000000f
         xlat
      
         push ecx
         mov cl,al                           
         call put_char
         pop ecx
       
         loop .xlt
      
         pop ds
         popad
         
         retf
      
;-------------------------------------------------------------------------------
set_up_gdt_descriptor:                      ;在GDT内安装一个新的描述符
                                            ;输入：EDX:EAX=描述符 
                                            ;输出：CX=描述符的选择子
         push eax
         push ebx
         push edx

         push ds
         push es

         mov ebx,core_data_seg_sel          ;切换到核心数据段
         mov ds,ebx

         sgdt [pgdt]                        ;以便开始处理GDT

         mov ebx,mem_0_4_gb_seg_sel
         mov es,ebx

         movzx ebx,word [pgdt]              ;GDT界限
         inc bx                             ;GDT总字节数，也是下一个描述符偏移
         add ebx,[pgdt+2]                   ;下一个描述符的线性地址

         mov [es:ebx],eax
         mov [es:ebx+4],edx

         add word [pgdt],8                  ;增加一个描述符的大小

         lgdt [pgdt]                        ;对GDT的更改生效

         mov ax,[pgdt]                      ;得到GDT界限值
         xor dx,dx
         mov bx,8
         div bx                             ;除以8，去掉余数
         mov cx,ax
         shl cx,3                           ;将索引号移到正确位置

         pop es
         pop ds

         pop edx
         pop ebx
         pop eax

         retf
;-------------------------------------------------------------------------------
make_seg_descriptor:                        ;构造存储器和系统的段描述符
                                            ;输入：EAX=线性基地址
                                            ;      EBX=段界限
                                            ;      ECX=属性。各属性位都在原始
                                            ;          位置，无关的位清零 
                                            ;返回：EDX:EAX=描述符
         mov edx,eax
         shl eax,16
         or ax,bx                           ;描述符前32位(EAX)构造完毕

         and edx,0xffff0000                 ;清除基地址中无关的位
         rol edx,8
         bswap edx                          ;装配基址的31~24和23~16  (80486+)

         xor bx,bx
         or edx,ebx                         ;装配段界限的高4位

         or edx,ecx                         ;装配属性

         retf

;-------------------------------------------------------------------------------
make_gate_descriptor:                       ;构造门的描述符（调用门等）
                                            ;输入：EAX=门代码在段内偏移地址
                                            ;       BX=门代码所在段的选择子 
                                            ;       CX=段类型及属性等（各属
                                            ;          性位都在原始位置）
                                            ;返回：EDX:EAX=完整的描述符
         push ebx
         push ecx
      
         mov edx,eax
         and edx,0xffff0000                 ;得到偏移地址高16位 
         or dx,cx                           ;组装属性部分到EDX
       
         and eax,0x0000ffff                 ;得到偏移地址低16位 
         shl ebx,16                          
         or eax,ebx                         ;组装段选择子部分
      
         pop ecx
         pop ebx
      
         retf                                   
                             
;-------------------------------------------------------------------------------
allocate_a_4k_page:                         ;分配一个4KB的页
                                            ;输入：无
                                            ;输出：EAX=页的物理地址
         push ebx
         push ecx
         push edx
         push ds
;332、333 行，先令段寄存器DS 指向内核数据段。
         mov eax,core_data_seg_sel
         mov ds,eax
;335～341 行，从头开始搜索位串，查找空闲的页。具体地说，就是找到第一个为“0”的比特，并记下它在整个位串中的位置。
         xor eax,eax ;335 行，先将EAX 寄存器清零，这表明我们要从位串的第1 个比特开始搜索。
  .b1: ;bts（Bit Test and Set）指令测试位串中的某比特，用该比特的值设置EFLAGS寄存器的CF标志，然后将该比特置“1”。  337 行，执行bts 指令。这将使指定的比特被传送到标志寄存器的CF 位，同时那一位被置“1”。置“1”是必做的工作，如果它原本就是“1”，这也没什么影响；如果它原本是“0”，那么，它就是我们要找的比特，它对应的页将被分配，而将它置“1”是应该的。
         bts [page_bit_map],eax    ;bts r/m16, r16  bts r/m32, r32  如果目的操作数是通用寄存器，那么，指定的位串就是该寄存器的内容（长度为16 比特或者32 比特）。在这种情况下，根据操作数的长度，处理器先求得源操作数除以16 或者32 的余数，并把它作为要测试的比特的索引。然后，从位串中取出该比特，传送到EFLAGS 寄存器的CF 位。最后，将该比特置位。
         jnc .b2 ;判断位串中指定的位是否原本为“0” ;则如果目的操作数是一个内存地址，那么，它给出的是位串在内存中的起始地址，或者说该位串第1 个字或者双字的地址。同样地，源操作数用于指定待测试的比特在串中的位置。因为串在内存中，所以其长度可以最大程度地延伸，具体的长度取决于源操作数的尺寸，毕竟它用于指定测试的位置。如果源操作数是16 位通用寄存器，位串最长可以达到2的16次方比特；如果源操作数是32 位的通用寄存器，则位串最长可以达到2的32次方比特。无论如何，在这种情况下，指令执行时，处理器会用目的操作数和源操作数得到被测比特所在的那个内存单元的线性地址。然后，取出该比特，传送到EFLAGS 寄存器的CF 位。最后，将原处的该比特置位。
         inc eax     ;将EAX 的内容加一
         cmp eax,page_map_len*8    ;判断是否已经测试了位串中的所有比特，以防止越界。  page_map_len 是一个用伪指令equ 声明的常数，位于第473 行
         jl .b1
;最坏的情况下，没有找到可以用于分配的空闲页，则显示一条错误消息，并停机。
         mov ebx,message_3
         call sys_routine_seg_sel:put_string
         hlt                                ;没有可以分配的页，停机 
         
  .b2: ;第348行，将该比特在位串中的位置数值乘以页的大小0x1000（或者十进制数4096），就是该比特所对应的那个页的物理地址。
         shl eax,12                         ;乘以4096（0x1000） 
         
         pop ds
         pop edx
         pop ecx
         pop ebx
         
         ret  ;返回时，页的物理地址位于EAX 寄存器中。
         
;在可用的物理内存中搜索空闲的页，并将它安装在当前的层次化分页结构中（页目录表和页表）。简单地说，就是寻找一个可用的页，然后，根据线性地址来创建页目录项和页表项，并将页的地址填写在页表项中。
alloc_inst_a_page:                          ;分配一个页，并安装在当前活动的
                                            ;层级分页结构中
                                            ;输入：EBX=页的线性地址
         push eax
         push ebx
         push esi
         push ds
         
         mov eax,mem_0_4_gb_seg_sel
         mov ds,eax  ;令段寄存器DS指向0～4GB的内存段（段的基地址是0x00000000）
         
;检查该线性地址所对应的页表是否存在  线性地址的高10 位是页目录表的索引，将该值乘以4，就是当前页目录表中，与该线性地址对应的页目录项。
         mov esi,ebx ;将EBX寄存器中的线性地址传送到ESI寄存器作为副本
         and esi,0xffc00000 ;用AND指令保留线性地址的高10位，其他各位清零
         shr esi,20                         ;得到页目录索引，并乘以4 
         or esi,0xfffff000                  ;页目录自身的线性地址+表内偏移 
;测试该目录项的P位，看它是否为“1”。如果为“1”，则表明对应的页表已经存在，只要在那个页表中添加一项即可；否则，必须先创建页表，并填写页目录项。
         test dword [esi],0x00000001        ;P位是否为“1”。检查该线性地址是 
         jnz .b1                            ;否已经有对应的页表
;如果对应的页目录项不存在，那么，将执行第379～381行的指令，以分配一个物理页作为页表，并将页的物理地址填写到页目录项内。
         ;创建该线性地址所对应的页表  调用allocate_a_4k_page过程的目的是分配一个页作为页表。
         call allocate_a_4k_page            ;分配一个页做为页表 eax返回页的物理地址
         or eax,0x00000007 ;US＝1，特权级别为3 的程序也可以访问；RW＝1，页是可读可写的；P＝1，页已经位于内存中，可以使用
         mov [esi],eax                      ;在页目录中登记该页表，作为目录项存在。
;剩下的工作就是为那个线性地址分配一个最终的页，并登记在页表内。
  .b1: ;386～388行，将ESI寄存器中的内容右移10次，清除两边，只保留中间的10位，同时，将高10位的内容改成二进制的1111111111（0x3FF）。这样一来，当页部件进行地址转换时，它用高10位的0x3FF乘以4去访问页目录表。由于此表项存放的是页目录表自己的物理地址，因此，此表项所指向的页表，正是当前页目录表自己，这实际上是把页目录表当成页表来用。
         ;开始访问该线性地址所对应的页表 
         mov esi,ebx  ;用于分配页的线性地址位于EBX寄存器中，这一行用于在ESI寄存器中制作它的一个副本。
         shr esi,10  ;将ESI寄存器中的内容右移10次，清除两边，只保留中间的10位
         and esi,0x003ff000                 ;或者0xfffff000，因高10位是零 
         or esi,0xffc00000                  ;得到该页表的线性地址   将高10位的内容改成二进制的1111111111（0x3FF）
         
         ;得到该线性地址在页表内的对应条目（页表项） 
         and ebx,0x003ff000 ;用and指令只保留中间的10 位，两边清零
         shr ebx,10                         ;相当于右移12位，再乘以4
         or esi,ebx                         ;得到页表项的线性地址，在ESI中
         call allocate_a_4k_page            ;分配一个页，并在EAX寄存器中返回页的物理地址，这才是要安装的页
         or eax,0x00000007 ;US＝1，特权级别为3 的程序也可以访问；RW＝1，页是可读可写的；P＝1，页已经位于内存中，可以使用
         mov [esi],eax 
          
         pop ds
         pop esi
         pop ebx
         pop eax
         
         retf  

;-------------------------------------------------------------------------------
create_copy_cur_pdir:                       ;创建新页目录，并复制当前页目录内容
                                            ;输入：无
                                            ;输出：EAX=新页目录的物理地址 
         push ds
         push es
         push esi
         push edi
         push ebx
         push ecx
         
         mov ebx,mem_0_4_gb_seg_sel
         mov ds,ebx
         mov es,ebx
         
         call allocate_a_4k_page            
         mov ebx,eax
         or ebx,0x00000007
         mov [0xfffffff8],ebx
         
         mov esi,0xfffff000                 ;ESI->当前页目录的线性地址
         mov edi,0xffffe000                 ;EDI->新页目录的线性地址
         mov ecx,1024                       ;ECX=要复制的目录项数
         cld
         repe movsd 
         
         pop ecx
         pop ebx
         pop edi
         pop esi
         pop es
         pop ds
         
         retf
         
;-------------------------------------------------------------------------------
terminate_current_task:                     ;终止当前任务
                                            ;注意，执行此例程时，当前任务仍在
                                            ;运行中。此例程其实也是当前任务的
                                            ;一部分 
         mov eax,core_data_seg_sel
         mov ds,eax

         pushfd
         pop edx
 
         test dx,0100_0000_0000_0000B       ;测试NT位
         jnz .b1                            ;当前任务是嵌套的，到.b1执行iretd 
         jmp far [program_man_tss]          ;程序管理器任务 
  .b1: 
         iretd

sys_routine_end:

;===============================================================================
SECTION core_data vstart=0                  ;系统核心的数据段 
;------------------------------------------------------------------------------- 
         pgdt             dw  0             ;用于设置和修改GDT 
                          dd  0
;始化了64字节的数据。这64字节首尾相连。用来表示512（64*8）个页的分配情况（已分配是1,未分配是0）
         page_bit_map     db  0xff,0xff,0xff,0xff,0xff,0x55,0x55,0xff ;前32字节差不多都是1，因为前
                          db  0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff ;32字节对应256个页
                          db  0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff ;那些页是最低端1MB
                          db  0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff ;内存的那些页。它们已经整体上划归内核使用了，没有被内核占用的部分多数也被外围硬件占用了，比如ROM-BIOS。
                          db  0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55
                          db  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00
                          db  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00
                          db  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00
         page_map_len     equ $-page_bit_map
                          
         ;符号地址检索表
         salt:
         salt_1           db  '@PrintString'
                     times 256-($-salt_1) db 0
                          dd  put_string
                          dw  sys_routine_seg_sel

         salt_2           db  '@ReadDiskData'
                     times 256-($-salt_2) db 0
                          dd  read_hard_disk_0
                          dw  sys_routine_seg_sel

         salt_3           db  '@PrintDwordAsHexString'
                     times 256-($-salt_3) db 0
                          dd  put_hex_dword
                          dw  sys_routine_seg_sel

         salt_4           db  '@TerminateProgram'
                     times 256-($-salt_4) db 0
                          dd  terminate_current_task
                          dw  sys_routine_seg_sel

         salt_item_len   equ $-salt_4
         salt_items      equ ($-salt)/salt_item_len

         message_0        db  '  Working in system core,protect mode.'
                          db  0x0d,0x0a,0

         message_1        db  '  Paging is enabled.System core is mapped to'
                          db  ' address 0x80000000.',0x0d,0x0a,0
         
         message_2        db  0x0d,0x0a
                          db  '  System wide CALL-GATE mounted.',0x0d,0x0a,0
         
         message_3        db  '********No more pages********',0
         
         message_4        db  0x0d,0x0a,'  Task switching...@_@',0x0d,0x0a,0
         
         message_5        db  0x0d,0x0a,'  Processor HALT.',0
         
        
         bin_hex          db '0123456789ABCDEF'
                                            ;put_hex_dword子过程用的查找表 

         core_buf   times 512 db 0          ;内核用的缓冲区

         cpu_brnd0        db 0x0d,0x0a,'  ',0
         cpu_brand  times 52 db 0
         cpu_brnd1        db 0x0d,0x0a,0x0d,0x0a,0

         ;任务控制块链
         tcb_chain        dd  0

         ;内核信息 初始化了一个双字0x80100000，这就是初始的可分配线性地址。
         core_next_laddr  dd  0x80100000    ;内核空间中下一个可分配的线性地址        
         program_man_tss  dd  0             ;程序管理器的TSS描述符选择子 
                          dw  0

core_data_end:
               
;===============================================================================
SECTION core_code vstart=0
;-------------------------------------------------------------------------------
fill_descriptor_in_ldt:                     ;在LDT内安装一个新的描述符
                                            ;输入：EDX:EAX=描述符
                                            ;          EBX=TCB基地址
                                            ;输出：CX=描述符的选择子
         push eax
         push edx
         push edi
         push ds

         mov ecx,mem_0_4_gb_seg_sel
         mov ds,ecx

         mov edi,[ebx+0x0c]                 ;获得LDT基地址
         
         xor ecx,ecx
         mov cx,[ebx+0x0a]                  ;获得LDT界限
         inc cx                             ;LDT的总字节数，即新描述符偏移地址
         
         mov [edi+ecx+0x00],eax
         mov [edi+ecx+0x04],edx             ;安装描述符

         add cx,8                           
         dec cx                             ;得到新的LDT界限值 

         mov [ebx+0x0a],cx                  ;更新LDT界限值到TCB

         mov ax,cx
         xor dx,dx
         mov cx,8
         div cx
         
         mov cx,ax
         shl cx,3                           ;左移3位，并且
         or cx,0000_0000_0000_0100B         ;使TI位=1，指向LDT，最后使RPL=00 

         pop ds
         pop edi
         pop edx
         pop eax
     
         ret
      
;-------------------------------------------------------------------------------
load_relocate_program:                      ;加载并重定位用户程序
                                            ;输入: PUSH 逻辑扇区号
                                            ;      PUSH 任务控制块基地址
                                            ;输出：无 
         pushad
      
         push ds
         push es
      
         mov ebp,esp                        ;为访问通过堆栈传递的参数做准备
;592～602行，用于将当前页目录表的前半部分清空。在每次创建一个新任务时，都应当清空内核页目录表的前512个目录项。
         mov ecx,mem_0_4_gb_seg_sel
         mov es,ecx
      
         ;清空当前页目录的前半部分（对应低2GB的局部地址空间） 
         mov ebx,0xfffff000  ;当前页目录表的线性地址是0xFFFFF000
         xor esi,esi  ;ESI寄存器用于提供每个表项的索引号，将索引号乘以4，再和基地址相加，就能得到每个目录项的线性地址
  .b1:
         mov dword [es:ebx+esi*4],0x00000000
         inc esi
         cmp esi,512 ;一共要处理512个表项。
         jl .b1
         
         ;以下开始分配内存并加载用户程序
         mov eax,core_data_seg_sel
         mov ds,eax                         ;切换DS到内核数据段
;608～610行，从栈中取得用户程序所在的逻辑扇区号，和内核缓冲区的首地
         mov eax,[ebp+12*4]                 ;从堆栈中取出用户程序起始扇区号
         mov ebx,core_buf                   ;读取程序头部数据
         call sys_routine_seg_sel:read_hard_disk_0 ;调用过程read_hard_disk_0，读取用户程序的第一个扇区。

         ;以下判断整个程序有多大
         mov eax,[core_buf]                 ;程序尺寸
         mov ebx,eax
         and ebx,0xfffff000                 ;使之4KB对齐 
         add ebx,0x1000                        
         test eax,0x00000fff                ;程序的大小正好是4KB的倍数吗? 
         cmovnz eax,ebx                     ;不是。使用凑整的结果
;620、621 行，将程序的大小右移12次，相当于除以4096，这得到的是它占用的页数。面要用ECX寄存器的内容来控制循环次数。
         mov ecx,eax
         shr ecx,12                         ;程序占用的总4KB页数 
         
         mov eax,mem_0_4_gb_seg_sel         ;切换DS到0-4GB的段
         mov ds,eax
;循环的目的是分配物理页，并以4KB为单位读取用户程序来填充页。这里不是一个循环，而是两个，而且是嵌套的，即外循环和内循环。外循环负责分配4KB页
         mov eax,[ebp+12*4]                 ;起始扇区号
         mov esi,[ebp+11*4]                 ;从堆栈中取得TCB的基地址 把ESI寄存器的内容置为用户程序TCB的基地址
  .b2:
         mov ebx,[es:esi+0x06]              ;取得可用的线性地址
         add dword [es:esi+0x06],0x1000
         call sys_routine_seg_sel:alloc_inst_a_page     ;分配一个4KB的页
       ;内循环。外循环每执行一次，内循环要完整地执行8次。
         push ecx
         mov ecx,8
  .b3: ;read_hard_disk_0需要两个参数，除了EAX寄存器中的逻辑扇区号外，段寄存器DS必须指向缓冲区所在的段，EBX寄存器必须指向缓冲区的线性地址
         call sys_routine_seg_sel:read_hard_disk_0
         inc eax
         loop .b3

         pop ecx
         loop .b2
;644～652行，在内核的地址空间内分配内存，创建用户任务的TSS。
         ;在内核地址空间内创建用户任务的TSS
         mov eax,core_data_seg_sel          ;切换DS到内核数据段
         mov ds,eax

         mov ebx,[core_next_laddr]          ;用户任务的TSS必须在全局空间上分配 
         call sys_routine_seg_sel:alloc_inst_a_page     ;分配一个4kb的页
         add dword [core_next_laddr],4096        ;下一个分配地址
         
         mov [es:esi+0x14],ebx              ;在TCB中填写TSS的线性地址，为了后面的代码访问TSS
         mov word [es:esi+0x12],103         ;在TCB中填写TSS的界限值 
;第655～658行，创建用户任务的局部描述符表（LDT）。LDT是任务私有的，要在它自己的虚拟地址空间里分配所需要内存空间。
         ;在用户任务的局部地址空间内创建LDT 
         mov ebx,[es:esi+0x06]              ;从TCB中取得可用的线性地址
         add dword [es:esi+0x06],0x1000
         call sys_routine_seg_sel:alloc_inst_a_page
         mov [es:esi+0x0c],ebx              ;填写LDT线性地址到TCB中 
;第661～667行，创建用户任务的代码段描述符，并登记到LDT中。
         ;建立程序代码段描述符
         mov eax,0x00000000 ;代码段描述符中的基地址是0x00000000
         mov ebx,0x000fffff ;段界限值是0x000FFFFF
         mov ecx,0x00c0f800                 ;4KB粒度的代码段描述符，特权级3
         call sys_routine_seg_sel:make_seg_descriptor
         mov ebx,esi                        ;TCB的基地址
         call fill_descriptor_in_ldt
         or cx,0000_0000_0000_0011B         ;设置选择子的特权级为3
;669、670 行，将代码段描述符的选择子登记到任务状态段（TSS）中。
         mov ebx,[es:esi+0x14]              ;从TCB中获取TSS的线性地址
         mov [es:ebx+76],cx                 ;填写TSS的CS域 
;第673 ～ 679 行， 创建用户任务的数据段描述符。
         ;建立程序数据段描述符
         mov eax,0x00000000 ;基地址为0x00000000
         mov ebx,0x000fffff ;段界限也是0x000FFFFF
         mov ecx,0x00c0f200                 ;4KB粒度的数据段描述符，特权级3
         call sys_routine_seg_sel:make_seg_descriptor
         mov ebx,esi                        ;TCB的基地址
         call fill_descriptor_in_ldt
         or cx,0000_0000_0000_0011B         ;设置选择子的特权级为3
;在平坦模型下，段寄存器DS、ES、FS和GS都指向同一个4GB数据段。因此，第681～685行，将刚才生成的数据段描述符选择子填写到TSS的DS、ES、FS和GS寄存器域中。
         mov ebx,[es:esi+0x14]              ;从TCB中获取TSS的线性地址
         mov [es:ebx+84],cx                 ;填写TSS的DS域 
         mov [es:ebx+72],cx                 ;填写TSS的ES域
         mov [es:ebx+88],cx                 ;填写TSS的FS域
         mov [es:ebx+92],cx                 ;填写TSS的GS域
;第688～690 行，在用户任务自己的虚拟地址空间内分配内存，分配了4KB，所以用户任务的固有栈就是4KB。
         ;将数据段作为用户任务的3特权级固有堆栈 
         mov ebx,[es:esi+0x06]              ;从TCB中取得可用的线性地址
         add dword [es:esi+0x06],0x1000
         call sys_routine_seg_sel:alloc_inst_a_page
;第692～695 行，将CX寄存器中的数据段选择子填写到TSS的SS寄存器域中，同时，填写TSS的ESP寄存器域。由于栈从内存的高端向低端推进，所以，ESP寄存器域的内容被指定为TCB中的下一个可分配的线性地址。
         mov ebx,[es:esi+0x14]              ;从TCB中获取TSS的线性地址
         mov [es:ebx+80],cx                 ;填写TSS的SS域
         mov edx,[es:esi+0x06]              ;堆栈的高端线性地址 
         mov [es:ebx+56],edx                ;填写TSS的ESP域 
;三个栈段的基地址也是0x00000000，也要创建为向上扩展的数据段，段界限为0x000FFFFF，粒度为4KB。当然，这三个栈段，其描述符的特权级别不同，段选择子也不一样。
         ;在用户任务的局部地址空间内创建0特权级堆栈
         mov ebx,[es:esi+0x06]              ;从TCB中取得可用的线性地址
         add dword [es:esi+0x06],0x1000
         call sys_routine_seg_sel:alloc_inst_a_page

         mov eax,0x00000000
         mov ebx,0x000fffff
         mov ecx,0x00c09200                 ;4KB粒度的堆栈段描述符，特权级0
         call sys_routine_seg_sel:make_seg_descriptor
         mov ebx,esi                        ;TCB的基地址
         call fill_descriptor_in_ldt
         or cx,0000_0000_0000_0000B         ;设置选择子的特权级为0

         mov ebx,[es:esi+0x14]              ;从TCB中获取TSS的线性地址
         mov [es:ebx+8],cx                  ;填写TSS的SS0域
         mov edx,[es:esi+0x06]              ;堆栈的高端线性地址
         mov [es:ebx+4],edx                 ;填写TSS的ESP0域 

         ;在用户任务的局部地址空间内创建1特权级堆栈
         mov ebx,[es:esi+0x06]              ;从TCB中取得可用的线性地址
         add dword [es:esi+0x06],0x1000
         call sys_routine_seg_sel:alloc_inst_a_page

         mov eax,0x00000000
         mov ebx,0x000fffff
         mov ecx,0x00c0b200                 ;4KB粒度的堆栈段描述符，特权级1
         call sys_routine_seg_sel:make_seg_descriptor
         mov ebx,esi                        ;TCB的基地址
         call fill_descriptor_in_ldt
         or cx,0000_0000_0000_0001B         ;设置选择子的特权级为1

         mov ebx,[es:esi+0x14]              ;从TCB中获取TSS的线性地址
         mov [es:ebx+16],cx                 ;填写TSS的SS1域
         mov edx,[es:esi+0x06]              ;堆栈的高端线性地址
         mov [es:ebx+12],edx                ;填写TSS的ESP1域 

         ;在用户任务的局部地址空间内创建2特权级堆栈
         mov ebx,[es:esi+0x06]              ;从TCB中取得可用的线性地址
         add dword [es:esi+0x06],0x1000
         call sys_routine_seg_sel:alloc_inst_a_page

         mov eax,0x00000000
         mov ebx,0x000fffff
         mov ecx,0x00c0d200                 ;4KB粒度的堆栈段描述符，特权级2
         call sys_routine_seg_sel:make_seg_descriptor
         mov ebx,esi                        ;TCB的基地址
         call fill_descriptor_in_ldt
         or cx,0000_0000_0000_0010B         ;设置选择子的特权级为2

         mov ebx,[es:esi+0x14]              ;从TCB中获取TSS的线性地址
         mov [es:ebx+24],cx                 ;填写TSS的SS2域
         mov edx,[es:esi+0x06]              ;堆栈的高端线性地址
         mov [es:ebx+20],edx                ;填写TSS的ESP2域 


         ;重定位SALT 
         mov eax,mem_0_4_gb_seg_sel         ;访问任务的4GB虚拟地址空间时用 
         mov es,eax                         
                                                    
         mov eax,core_data_seg_sel
         mov ds,eax
      
         cld

         mov ecx,[es:0x0c]                  ;U-SALT条目数 
         mov edi,[es:0x08]                  ;U-SALT在4GB空间内的偏移 
  .b4:
         push ecx
         push edi
      
         mov ecx,salt_items
         mov esi,salt
  .b5:
         push edi
         push esi
         push ecx

         mov ecx,64                         ;检索表中，每条目的比较次数 
         repe cmpsd                         ;每次比较4字节 
         jnz .b6
         mov eax,[esi]                      ;若匹配，则esi恰好指向其后的地址
         mov [es:edi-256],eax               ;将字符串改写成偏移地址 
         mov ax,[esi+4]
         or ax,0000000000000011B            ;以用户程序自己的特权级使用调用门
                                            ;故RPL=3 
         mov [es:edi-252],ax                ;回填调用门选择子 
  .b6:
      
         pop ecx
         pop esi
         add esi,salt_item_len
         pop edi                            ;从头比较 
         loop .b5
      
         pop edi
         add edi,256
         pop ecx
         loop .b4
;第797～803 行，创建LDT描述符，并登记在GDT中。
         ;在GDT中登记LDT描述符
         mov esi,[ebp+11*4]                 ;从堆栈中取得TCB的基地址
         mov eax,[es:esi+0x0c]              ;LDT的起始线性地址
         movzx ebx,word [es:esi+0x0a]       ;LDT段界限
         mov ecx,0x00408200                 ;LDT描述符，特权级0
         call sys_routine_seg_sel:make_seg_descriptor
         call sys_routine_seg_sel:set_up_gdt_descriptor
         mov [es:esi+0x10],cx               ;登记LDT选择子到TCB中
;第805～820 行，填写任务状态段（TSS）的其余部分。包括LDT 选择子域、I/O 位映射区的偏移地址、前一任务的TSS 链接域、TSS 的界限、EIP 和EFLAGS 寄存器域。EIP 域填写的是用户程序的入口点，从内核任务切换到用户任务时，是用TSS 中的内容恢复现场的，所以这关系到任务应该从哪里开始执行。EFLAGS 域的内容是当前内核任务EFLAGS 寄存器的副本。
         mov ebx,[es:esi+0x14]              ;从TCB中获取TSS的线性地址
         mov [es:ebx+96],cx                 ;填写TSS的LDT域 

         mov word [es:ebx+0],0              ;反向链=0
      
         mov dx,[es:esi+0x12]               ;段长度（界限）
         mov [es:ebx+102],dx                ;填写TSS的I/O位图偏移域 
      
         mov word [es:ebx+100],0            ;T=0
      
         mov eax,[es:0x04]                  ;从任务的4GB地址空间获取入口点 
         mov [es:ebx+32],eax                ;填写TSS的EIP域 

         pushfd
         pop edx
         mov [es:ebx+36],edx                ;填写TSS的EFLAGS域 
;第823～828 行，创建TSS 描述符，并登记到GDT 中。处理器要求TSS 描述符必须登记在GDT 中。TSS 描述符的特权级DPL 必须是0，只有当前特权级别为0 的程序才能转换到该任务。
         ;在GDT中登记TSS描述符
         mov eax,[es:esi+0x14]              ;从TCB中获取TSS的起始线性地址
         movzx ebx,word [es:esi+0x12]       ;段长度（界限）
         mov ecx,0x00408900                 ;TSS描述符，特权级0
         call sys_routine_seg_sel:make_seg_descriptor
         call sys_routine_seg_sel:set_up_gdt_descriptor
         mov [es:esi+0x18],cx               ;登记TSS选择子到TCB

         ;创建用户任务的页目录
         ;注意！页的分配和使用是由页位图决定的，可以不占用线性地址空间 
         call sys_routine_seg_sel:create_copy_cur_pdir  ;406行
         mov ebx,[es:esi+0x14]              ;从TCB中获取TSS的线性地址
         mov dword [es:ebx+28],eax          ;填写TSS的CR3(PDBR)域
                   
         pop es                             ;恢复到调用此过程前的es段 
         pop ds                             ;恢复到调用此过程前的ds段
      
         popad
      
         ret 8                              ;丢弃调用本过程前压入的参数 
      
;-------------------------------------------------------------------------------
append_to_tcb_link:                         ;在TCB链上追加任务控制块
                                            ;输入：ECX=TCB线性基地址
         push eax
         push edx
         push ds
         push es
         
         mov eax,core_data_seg_sel          ;令DS指向内核数据段 
         mov ds,eax
         mov eax,mem_0_4_gb_seg_sel         ;令ES指向0..4GB段
         mov es,eax
         
         mov dword [es: ecx+0x00],0         ;当前TCB指针域清零，以指示这是最
                                            ;后一个TCB
                                             
         mov eax,[tcb_chain]                ;TCB表头指针
         or eax,eax                         ;链表为空？
         jz .notcb 
         
  .searc:
         mov edx,eax
         mov eax,[es: edx+0x00]
         or eax,eax               
         jnz .searc
         
         mov [es: edx+0x00],ecx
         jmp .retpc
         
  .notcb:       
         mov [tcb_chain],ecx                ;若为空表，直接令表头指针指向TCB
         
  .retpc:
         pop es
         pop ds
         pop edx
         pop eax
         
         ret
         
;-------------------------------------------------------------------------------
start: ;令段寄存器DS 和ES 分别指向内核数据段与0～4GB 数据段，以方便后面的操作。
         mov ecx,core_data_seg_sel          ;令DS指向核心数据段 
         mov ds,ecx

         mov ecx,mem_0_4_gb_seg_sel         ;令ES指向4GB数据段 
         mov es,ecx
       ;在屏幕上显示第一个字符串，表明当前正在内核中执行，而且是在保护模式下工作。
         mov ebx,message_0                    
         call sys_routine_seg_sel:put_string
                                         
         ;显示处理器品牌信息 
         mov eax,0x80000002
         cpuid
         mov [cpu_brand + 0x00],eax
         mov [cpu_brand + 0x04],ebx
         mov [cpu_brand + 0x08],ecx
         mov [cpu_brand + 0x0c],edx
      
         mov eax,0x80000003
         cpuid
         mov [cpu_brand + 0x10],eax
         mov [cpu_brand + 0x14],ebx
         mov [cpu_brand + 0x18],ecx
         mov [cpu_brand + 0x1c],edx

         mov eax,0x80000004
         cpuid
         mov [cpu_brand + 0x20],eax
         mov [cpu_brand + 0x24],ebx
         mov [cpu_brand + 0x28],ecx
         mov [cpu_brand + 0x2c],edx

         mov ebx,cpu_brnd0                  ;显示处理器品牌信息 
         call sys_routine_seg_sel:put_string
         mov ebx,cpu_brand
         call sys_routine_seg_sel:put_string
         mov ebx,cpu_brnd1
         call sys_routine_seg_sel:put_string

         ;准备打开分页机制
;首先必须创建页目录和页表。每个任务都有自己的页目录和页表，内核也不例外，尽管它是为所有任务所共有的，但也包括作为任务而独立存在的部分，以执行必要的系统管理工作。因此，要想内核正常运行，必须创建它自己的页目录和页表。
         ;创建系统内核的页目录表PDT
         ;页目录表清零 927～933 行，访问段寄存器ES所指向的4GB数据段，用0x00020000作为偏移量，访问页目录，将所有目录项清零。
         mov ecx,1024                       ;1024个目录项
         mov ebx,0x00020000                 ;页目录的物理地址
         xor esi,esi
  .b1:
         mov dword [es:ebx+esi],0x00000000  ;页目录表项清零 
         add esi,4
         loop .b1
;936 行，将页目录表的物理地址登记在它自己的最后一个目录项内。页目录最大4KB，最后一个目录项的偏移量是0xFFC，即十进制数4092。填写的内容是0x00020003，该数值的前20 位是物理地址的高20 位；P＝1，页是位于内存中的；RW＝1，该目录项指向的页表可读可写。US 位是“0”，故此目录项指向的页表不允许特权级为3 的程序和任务访问。         
         ;在页目录内创建指向页目录自己的目录项
         mov dword [es:ebx+4092],0x00020003 
;939 行，修改页目录内第1 个目录项的内容，使其指向页表，页表的物理地址是0x00021000，该页位于内存中，可读可写，但不允许特权级别为3 的程序和任务访问。
         ;在页目录内创建与线性地址0x00000000对应的目录项
         mov dword [es:ebx+0],0x00021003    ;写入目录项（页表的物理地址和属性）      
;第942～952 行，将内存低端1MB 所包含的那些页的物理地址按顺序一个一个地填写到页表中，当然，仅填写256 个页表项。第1 个页表项对应的是线性地址0x00000000～0x00000FFF，填写的内容是第1 个页的物理地址0x00000000；第2 个页表项对应的是线性地址0x00001000～0x00001FFF，填写的是第2 个页的物理地址0x00001000；...
         ;创建与上面那个目录项相对应的页表，初始化页表项 
         mov ebx,0x00021000                 ;页表的物理地址    用EBX寄存器指向页表基地址
         xor eax,eax                        ;起始页的物理地址  用EAX寄存器保存页的物理地址，初始为0x00000000，每次按0x1000递增，以指向下一个页
         xor esi,esi        ;ESI寄存器用于定位每一个页表项。
  .b2:       
         mov edx,eax
         or edx,0x00000003  ;P＝1，RW＝1；US＝0，特权级别为3的程序和任务不能访问这些页。
         mov [es:ebx+esi*4],edx             ;登记页的物理地址
         add eax,0x1000                     ;下一个相邻页的物理地址 
         inc esi
         cmp esi,256                        ;仅低端1MB内存对应的页才是有效的 
         jl .b2
;接着处理其余的表项，使它们的内容为全零。即，将它们置为无效表项。
  .b3:                                      ;其余的页表项置为无效
         mov dword [es:ebx+esi*4],0x00000000  
         inc esi
         cmp esi,1024
         jl .b3 
;961、962 行，将页目录表的物理基地址传送到控制寄存器CR3，也就是页目录表基地址寄存器PDBR
         ;令CR3寄存器指向页目录，并正式开启页功能 
         mov eax,0x00020000                 ;PCD=PWT=0  
         mov cr3,eax ;高20位保存页目录基地址，低12位除了PCD和PWT位外都没有使用。3是PWT，4是PCD。这两位用于控制页目录的高速缓存特性
;控制寄存器CR0 的最高位，也就是位31，是PG（Page）位，用于开启或者关闭页功能。当该位清零时，页功能被关闭，从段部件来的线性地址就是物理地址；当它置位时，页功能开启。只能在保护模式下才能开启页功能，当PE 位清零时（实模式），设置PG 位将导致处理器产生一个异常中断。
         mov eax,cr0 ;先读取控制寄存器CR0 的原始内容
         or eax,0x80000000  ;将其最高位置“1”，其他各位保持原来的数值不变
         mov cr0,eax                        ;开启分页机制
;969～973 行，在内核的页目录表中，创建一个和线性地址0x80000000 对应的目录项，并使它指向同一个页表。毕竟，我们只改变了线性地址空间范围，内核的数据和代码仍然在原来的页内，没有改变。
         ;在页目录内创建与线性地址0x80000000对应的目录项
         mov ebx,0xfffff000                 ;页目录自己的线性地址  969 行，实际上给出了当前正在使用的页目录表的线性基地址0xFFFFF000
         mov esi,0x80000000                 ;映射的起始地址       970 行，给出了要修改的那个目录项所对应的线性基地址，其高10 位的值乘以4，决定了该线性地址所对应的页目录表内偏移量
         shr esi,22                         ;线性地址的高10位是目录索引  971 行，将线性地址右移22 位，只保留高10 位
         shl esi,2   ;972 行，再将它左移2 位，相当于乘以4。
         mov dword [es:ebx+esi],0x00021003  ;写入目录项（页表的物理地址和属性）
                                            ;目标单元的线性地址为0xFFFFF200
                                             
         ;将GDT中的段描述符映射到线性地址0x80000000
         sgdt [pgdt]
;先取得GDT的线性基地址，并传送到EBX寄存器，准备开始访问GDT内的段描述符。
         mov ebx,[pgdt+2]
;依次找到内核栈段、文本模式下的视频缓冲区段、公共例程段、内核数据段和内核代码段的描述符，并将每个描述符的最高位改成“1”。在这里，EBX寄存器提供了GDT的基地址
         or dword [es:ebx+0x10+4],0x80000000  ;0x10、0x18、0x20 等这些数提供了每个
         or dword [es:ebx+0x18+4],0x80000000  ;描述符在表内的偏移量
         or dword [es:ebx+0x20+4],0x80000000  ;在偏移量的基础上加4，就是每个描述符的高32位
         or dword [es:ebx+0x28+4],0x80000000
         or dword [es:ebx+0x30+4],0x80000000
         or dword [es:ebx+0x38+4],0x80000000
;第988 行，将GDT的基地址映射到内存的高端，即加上0x80000000
         add dword [pgdt+2],0x80000000      ;GDTR也用的是线性地址 
;第990 行，将修改后的GDT 基地址和界限值加载到全局描述符表寄存器（GDTR），使修改生效。
         lgdt [pgdt]
;第992 行，使用远转移指令jmp 跳转到下一条指令的位置接着执行。这将导致处理器用新的段描述符选择子core_code_seg_sel（0x38）访问GDT，从中取出修改后的内核代码段描述符，并加载到其描述符高速缓存器中。同时，这也直接导致处理器开始从内存的高端位置取指令执行。
         jmp core_code_seg_sel:flush        ;刷新段寄存器CS，启用高端线性地址 
;第995～999 行，重新加载段寄存器SS 和DS 的描述符高速缓存器，使它们的内容变成修改后的数据段描述符。注意，这些段在内存中的物理位置并没有改变。特别是栈段，因为仅仅是线性地址变了，栈在内存中的物理位置并没有发生变化，所以栈指针寄存器ESP 仍指向正确的位置。段寄存器ES 没有修改，因为它指向整个0～4GB 内存段，内核需要有访问整个内存空间的能力。段寄存器FS 和GS 没有使用。
   flush:
         mov eax,core_stack_seg_sel
         mov ss,eax
         
         mov eax,core_data_seg_sel
         mov ds,eax
;显示一条消息，告诉屏幕前的人，已经开启了分页功能，而且内核已经被映射到线性地址0x80000000以上。
         mov ebx,message_1
         call sys_routine_seg_sel:put_string
;第1005～1023 行，安装供用户程序使用的调用门，并显示安装成功的消息。门描述符只涉及目标代码段的选择子和偏移量，但是你应该清楚，目标代码实际上已经被映射到内存的高端了。
         ;以下开始安装为整个系统服务的调用门。特权级之间的控制转移必须使用门
         mov edi,salt                       ;C-SALT表的起始位置 
         mov ecx,salt_items                 ;C-SALT表的条目数量 
  .b4:
         push ecx   
         mov eax,[edi+256]                  ;该条目入口点的32位偏移地址 
         mov bx,[edi+260]                   ;该条目入口点的段选择子 
         mov cx,1_11_0_1100_000_00000B      ;特权级3的调用门(3以上的特权级才
                                            ;允许访问)，0个参数(因为用寄存器
                                            ;传递参数，而没有用栈) 
         call sys_routine_seg_sel:make_gate_descriptor
         call sys_routine_seg_sel:set_up_gdt_descriptor
         mov [edi+260],cx                   ;将返回的门描述符选择子回填
         add edi,salt_item_len              ;指向下一个C-SALT条目 
         pop ecx
         loop .b4

         ;对门进行测试 
         mov ebx,message_2
         call far [salt_1+256]              ;通过门显示信息(偏移量将被忽略) 
      
         ;为程序管理器的TSS分配内存空间   1026行，先访问内核数据段，从标号处取得当前可用的线性地址，将来要作为内核TSS的起始线性地址
         mov ebx,[core_next_laddr] ;529行，始化了一个双字0x80100000，这就是初始的可分配线性地址。
         call sys_routine_seg_sel:alloc_inst_a_page ;将EBX寄存器中的线性地址作为参数，调用过程alloc_inst_a_page去申请一个物理页。
         add dword [core_next_laddr],4096 ;将标号core_ next_laddr处的数据修改为下一个可分配的起始线性地址。
;1031～1038行，填写和初始化TSS中的静态部分，有些内容，比如CR3寄存器域，对任务的执行来说很关键，必须事先予以填写。
         ;在程序管理器的TSS中设置必要的项目 
         mov word [es:ebx+0],0              ;反向链=0

         mov eax,cr3
         mov dword [es:ebx+28],eax          ;登记CR3(PDBR)

         mov word [es:ebx+96],0             ;没有LDT。处理器允许没有LDT的任务。
         mov word [es:ebx+100],0            ;T=0
         mov word [es:ebx+102],103          ;没有I/O位图。0特权级事实上不需要。
;1041～1046 行，创建内核任务的TSS 描述符，并安装到GDT 中。TSS 描述符选择子保存在内核数据段中，位于第530 行，在那里，声明了标号program_man_tss 并初始化了1 个字。在任务切换时，需要使用它。
         ;创建程序管理器的TSS描述符，并安装到GDT中 
         mov eax,ebx                        ;TSS的起始线性地址
         mov ebx,103                        ;段长度（界限）
         mov ecx,0x00408900                 ;TSS描述符，特权级0
         call sys_routine_seg_sel:make_seg_descriptor  ;创建内核任务的TSS描述符
         call sys_routine_seg_sel:set_up_gdt_descriptor  ;安装到GDT中
         mov [program_man_tss+4],cx         ;保存程序管理器的TSS描述符选择子  第530行，在任务切换时，需要使用它。

         ;任务寄存器TR中的内容是任务存在的标志，该内容也决定了当前任务是谁。
         ;下面的指令为当前正在执行的0特权级任务“程序管理器”后补手续（TSS）。
         ltr cx  ;将当前任务的TSS描述符传送到任务寄存器TR。

         ;现在可认为“程序管理器”任务正执行中

;创建用户任务的任务控制块  1055～1057行，用于在内核的虚拟地址空间里分配4KB的内存（页）
         mov ebx,[core_next_laddr]
         call sys_routine_seg_sel:alloc_inst_a_page ;将EBX寄存器中的线性地址作为参数，调用过程alloc_inst_a_page去申请一个物理页。
         add dword [core_next_laddr],4096  ;将标号core_ next_laddr处的数据修改为下一个可分配的起始线性地址。
;1059～1062行，初始化TCB，为某些域赋初值
         mov dword [es:ebx+0x06],0          ;用户任务局部空间的分配从0开始。 0x06-0x09存下一个可用的线性地址。
         mov word [es:ebx+0x0a],0xffff      ;登记LDT初始的界限到TCB中  LDT的界限(0x0a-0x0b)是LDT的长度减一，LDT 的初始长度为0，因此，其界限值是0xFFFF。
         mov ecx,ebx
         call append_to_tcb_link            ;将此TCB添加到TCB链中 
;压入用户程序的TCB基地址和起始逻辑扇区号
         push dword 50                      ;用户程序位于逻辑50扇区
         push ecx                           ;压入任务控制块起始线性地址 
;调用过程load_relocate_program加载和重定位用户程序，并创建为一个任务。
         call load_relocate_program         
      
         mov ebx,message_4
         call sys_routine_seg_sel:put_string
         
         call far [es:ecx+0x14]             ;执行任务切换。
         
         mov ebx,message_5
         call sys_routine_seg_sel:put_string

         hlt
            
core_code_end:

;-------------------------------------------------------------------------------
SECTION core_trail
;-------------------------------------------------------------------------------
core_end: