         ;代码清单13-2
         ;文件名：c13_core.asm
         ;文件说明：保护模式微型核心程序 
         ;创建日期：2011-10-26 12:11

         ;以下常量定义部分。内核的大部分内容都应当固定 
         core_code_seg_sel     equ  0x38    ;内核代码段选择子
         core_data_seg_sel     equ  0x30    ;内核数据段选择子 
         sys_routine_seg_sel   equ  0x28    ;系统公共例程代码段的选择子 
         video_ram_seg_sel     equ  0x20    ;视频显示缓冲区的段选择子，0xb8000
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


         core_entry       dd start          ;核心代码段入口点#10      段内偏移，将来会传送到指令指针寄存器EIP
                          dw core_code_seg_sel   ;指定一个内存代码段的选择子（第7行定义）

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
         retf                               ;段间返回   通过retf返回，只能通过远过程调用来进入。

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
         mov eax,video_ram_seg_sel          ;0xb8000段的选择子
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
       ;每次返回ebx+512
         pop edx
         pop ecx
         pop eax
      
         retf                               ;段间返回   通过retf返回，只能通过远过程调用来进入。

;-------------------------------------------------------------------------------
;汇编语言程序是极难一次成功，而且调试非常困难。这个例程可以提供帮助 
put_hex_dword:                              ;在当前光标处以十六进制形式显示
                                            ;一个双字并推进光标 
                                            ;输入：EDX=要转换并显示的数字
                                            ;输出：无
         pushad      ;pushad指令会将32位模式下的所有常规寄存器（EAX、EBX、ECX、EDX、ESI、EDI、EBP、ESP）的值按照顺序压入栈中
         push ds     ;EDX寄存器是32位的，从右到左，将它以4位为一组，分成8组。每一组的值都在0～15（0x0～0xf）之间，我们把它转换成相应的字符’0’ ～’F’即可。
      
         mov ax,core_data_seg_sel           ;切换到核心数据段 
         mov ds,ax
      
         mov ebx,bin_hex                    ;指向核心数据段内的转换表        定义于374
         mov ecx,8
  .xlt:    
         rol edx,4
         mov eax,edx
         and eax,0x0000000f ;用and 指令保留低4 位，高位清零
         xlat ;该指令要求事先在DS:(E)BX 处定义一个用于转换编码的表格，指令执行时，处理器访问该表格，用AL寄存器的内容作为偏移量，从表格中取出一字节，传回AL寄存器。
       ;xlat指令用AL寄存器中的值作为索引访问对照表，取出相应的字符，并回传到AL寄存器。
         push ecx    ;每次从检索（对照）表中得到一个字符，就要调用put_char过程显示它。但put_char过程需要使用CL寄存器作为参数。因此，第220 行，在显示之前先要将ECX寄存器压栈保护。
         mov cl,al                           
         call put_char
         pop ecx
       
         loop .xlt
      
         pop ds
         popad       ;popad则会将栈中的值按照相反的顺序弹出并存储回这些寄存器中。
         retf        ;通过retf返回，只能通过远过程调用来进入。
      
;-------------------------------------------------------------------------------
allocate_memory:                            ;分配内存
                                            ;输入：ECX=希望分配的字节数
                                            ;输出：ECX=起始线性地址 
         push ds
         push eax
         push ebx
      
         mov eax,core_data_seg_sel ;使段寄存器DS指向内核数据段以访问标号ram_alloc所指向的内存单元
         mov ds,eax
      
         mov eax,[ram_alloc]       ;定义于335行，下一次分配时的起始地址
         add eax,ecx                        ;下一次分配时的起始地址 = 要分配的内存+本次起始地址
      
         ;这里应当有检测可用内存数量的指令
          
         mov ecx,[ram_alloc]                ;返回分配的起始地址

         mov ebx,eax               ;先将EAX寄存器的内容传送到EBX进行备份
         and ebx,0xfffffffc        ;4字节对齐，访问速度最快
         add ebx,4                          ;强制对齐 
         test eax,0x00000003                ;下次分配的起始地址最好是4字节对齐   看一看原始分配的起始地址（在EAX 寄存器中）是否是4字节对齐的
         cmovnz eax,ebx                     ;如果没有对齐，则强制对齐 
         mov [ram_alloc],eax                ;下次从该地址分配内存
                                            ;cmovcc指令可以避免控制转移 
         pop ebx
         pop eax
         pop ds

         retf        ;通过retf返回，只能通过远过程调用来进入。

;-------------------------------------------------------------------------------
set_up_gdt_descriptor:                      ;在GDT内安装一个新的描述符
                                            ;输入：EDX:EAX=描述符 
                                            ;输出：CX=描述符的选择子
         push eax
         push ebx
         push edx
      
         push ds
         push es
      
         mov ebx,core_data_seg_sel          ;切换到核心数据段  使ds指向内核数据段
         mov ds,ebx

         sgdt [pgdt]                        ;以便开始处理GDT   332行定义的pgdt

         mov ebx,mem_0_4_gb_seg_sel       ;定义在12行，整个0-4GB内存的段的选择子
         mov es,ebx                       ;使段寄存器ES指向4GB内存段以操作全局描述符表（GDT）。
       ;GDT的界限是16位的，似乎不需要使用32位的寄存器EBX。后面要用它来计算新描述符的32位线性地址，加法指令add要求的是两个32位操作数
         movzx ebx,word [pgdt]              ;GDT界限    进行0拓展
         inc bx                             ;GDT总字节数，也是下一个描述符偏移      使用bx，而非ebx。因为EBX寄存器中的内容是GDT的界限值0x0000FFFF，如果执行的是指令inc ebx。那么，EBX寄存器中的内容将是0x00010000
         add ebx,[pgdt+2]                   ;下一个描述符的线性地址 
       ;访问段寄存器ES所指向的4GB内存段，将EDX:EAX中的64位描述符写入由EBX寄存器所指向的偏移处。
         mov [es:ebx],eax
         mov [es:ebx+4],edx
      
         add word [pgdt],8                  ;增加一个描述符的大小   
      
         lgdt [pgdt]                        ;对GDT的更改生效 
       ;第292～297行，根据GDT的新界限值，来生成相应的段选择子
         mov ax,[pgdt]                      ;得到GDT界限值
         xor dx,dx
         mov bx,8
         div bx                             ;除以8，去掉余数，商就是我们所要得到的描述符索引号
         mov cx,ax                          ;将段选择子存入cx中
         shl cx,3                           ;将索引号移到正确位置 

         pop es
         pop ds

         pop edx
         pop ebx
         pop eax
      
         retf        ;通过retf返回，只能通过远过程调用来进入。
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

         retf        ;通过retf返回，只能通过远过程调用来进入。

;===============================================================================
SECTION core_data vstart=0                  ;系统核心的数据段
;-------------------------------------------------------------------------------
         pgdt             dw  0             ;用于设置和修改GDT 保存GDT的界限（大小）
                          dd  0           ;保存GDT的32位物理地址

         ram_alloc        dd  0x00100000    ;下次分配内存时的起始地址

         ;符号地址检索表
         salt:
         salt_1           db  '@PrintString'
                     times 256-($-salt_1) db 0
                          dd  put_string                ;put_string例程的偏移地址
                          dw  sys_routine_seg_sel       ;公共例程段的选择子

         salt_2           db  '@ReadDiskData'
                     times 256-($-salt_2) db 0
                          dd  read_hard_disk_0
                          dw  sys_routine_seg_sel

         salt_3           db  '@PrintDwordAsHexString'
                     times 256-($-salt_3) db 0
                          dd  put_hex_dword
                          dw  sys_routine_seg_sel

         salt_4           db  '@TerminateProgram'       ;当用户程序调用该过程时，意味着结束用户程序，将控制返回到内核。
                     times 256-($-salt_4) db 0
                          dd  return_point
                          dw  core_code_seg_sel

         salt_item_len   equ $-salt_4     ;每个条目的长度（字节数），数值上等于262。
         salt_items      equ ($-salt)/salt_item_len     ;用整个C-SALT的长度，除以每个条目的长度，就是条目的个数。
              ;如果你看到这段信息，那么这意味着我们正在保护模式下运行，内核已经加载，而且显示例程工作得也很完美。
         message_1        db  '  If you seen this message,that means we '
                          db  'are now in protect mode,and the system '
                          db  'core is loaded,and the video display '
                          db  'routine works perfectly.',0x0d,0x0a,0

         message_5        db  '  Loading user program...',0
         
         do_status        db  'Done.',0x0d,0x0a,0
         
         message_6        db  0x0d,0x0a,0x0d,0x0a,0x0d,0x0a
                          db  '  User program terminated,control returned.',0

         bin_hex          db '0123456789ABCDEF'
                                            ;put_hex_dword子过程用的查找表 
         core_buf   times 2048 db 0         ;内核用的缓冲区

         esp_pointer      dd 0              ;内核用来临时保存自己的栈指针     

         cpu_brnd0        db 0x0d,0x0a,'  ',0
         cpu_brand  times 52 db 0  ;定义52个空字节，存储cpuid返回的48个字节信息
         cpu_brnd1        db 0x0d,0x0a,0x0d,0x0a,0

;===============================================================================
SECTION core_code vstart=0
;-------------------------------------------------------------------------------
load_relocate_program:                      ;加载并重定位用户程序
                                            ;输入：ESI=起始逻辑扇区号
                                            ;返回：AX=指向用户程序头部的选择子 
         push ebx
         push ecx
         push edx
         push esi
         push edi
      
         push ds
         push es
       ;预先读入一个扇区，并判断用户程序的大小
         mov eax,core_data_seg_sel        ;定义在第8行，内核数据段的段选择子
         mov ds,eax                         ;切换DS到内核数据段
       
         mov eax,esi                        ;读取程序头部数据  esi存的起始逻辑扇区号，将esi给eax
         mov ebx,core_buf                 ;内核缓冲区，定义于376行，2048个字节
         call sys_routine_seg_sel:read_hard_disk_0      ;eax存逻辑扇区号，ebx存数据段偏移地址，将其用户程序加载到内核缓冲区core_buf中

         ;以下判断整个程序有多大
         mov eax,[core_buf]                 ;程序尺寸   用户程序的总大小就在头部内偏移量为0x00的地方
         mov ebx,eax        ;通过以下方式，来判断用户程序扇区多大，和之前的不同，这种方式可以避免使用条件转移指令，提高效率
         and ebx,0xfffffe00                 ;使之512字节对齐（能被512整除的数， 
         add ebx,512                        ;低9位都为0 
         test eax,0x000001ff                ;程序的大小正好是512的倍数吗? 
         cmovnz eax,ebx                     ;不是。使用凑整的结果     不为0则传送
      
         mov ecx,eax                        ;实际需要申请的内存数量
         call sys_routine_seg_sel:allocate_memory       ;定义于232行，通过ECX寄存器传入希望分配的字节数。当过程返回时，ECX寄存器包含了所分配内存的起始物理地址。335行声明了一个标号，为下次内存分配的起始地址
         mov ebx,ecx                        ;ebx -> 申请到的内存首地址   将ECX寄存器的内容传送到EBX，将其作为起始地址从硬盘上加载整个用户程序。
         push ebx                           ;保存该首地址      用于在后面访问用户程序头部。
         xor edx,edx        ;418-420用户程序的总长度除以512，得到它所占用的扇区总数。
         mov ecx,512
         div ecx
         mov ecx,eax                        ;总扇区数   eax存的总扇区数，将其存入ecx控制循环
      
         mov eax,mem_0_4_gb_seg_sel         ;切换DS到0-4GB的段   使段寄存器DS指向4GB的内存段，这样就可以加载用户程序了。
         mov ds,eax

         mov eax,esi                        ;起始扇区号        将起始逻辑扇区号给eax，以开始调用read_hard_disk_0读用户程序
  .b1: ;循环读取硬盘以加载用户程序。读取的次数由ECX控制；加载之前，其首地址已经位于EBX寄存器。起始逻辑扇区号原本是通过ESI寄存器传入的，循环开始之前已经传送到EAX寄存器（第426行）。
         call sys_routine_seg_sel:read_hard_disk_0
         inc eax
         loop .b1                           ;循环读，直到读完整个用户程序

         ;建立程序头部段描述符     434～438 行，读用户程序头部信息，根据这些信息创建头部段描述符。
         pop edi                            ;恢复程序装载的首地址     弹出的信息为417行压入的用户程序头部的起始地址
         mov eax,edi                        ;程序头部起始线性地址
         mov ebx,[edi+0x04]                 ;段长度
         dec ebx                            ;段界限 
         mov ecx,0x00409200                 ;字节粒度的数据段描述符
         call sys_routine_seg_sel:make_seg_descriptor   ;308行定义，用来构造段描述符，返回后EDX：EAX包含64位段描述符
         call sys_routine_seg_sel:set_up_gdt_descriptor ;263行定义，通过EDX:EAX传入描述符作为唯一的参数。该过程返回时，CX寄存器中包含了那个描述符的选择子。
         mov [edi+0x04],cx         ;cx为set_up_gdt_descriptor的返回值，保存的段选择子

         ;建立程序代码段描述符
         mov eax,edi
         add eax,[edi+0x14]                 ;代码起始线性地址
         mov ebx,[edi+0x18]                 ;段长度
         dec ebx                            ;段界限
         mov ecx,0x00409800                 ;字节粒度的代码段描述符
         call sys_routine_seg_sel:make_seg_descriptor   ;308行定义，用来构造段描述符，返回后EDX：EAX包含64位段描述符
         call sys_routine_seg_sel:set_up_gdt_descriptor ;263行定义，通过EDX:EAX传入描述符作为唯一的参数。该过程返回时，CX寄存器中包含了那个描述符的选择子。
         mov [edi+0x14],cx         ;cx为set_up_gdt_descriptor的返回值，保存的段选择子

         ;建立程序数据段描述符
         mov eax,edi
         add eax,[edi+0x1c]                 ;数据段起始线性地址
         mov ebx,[edi+0x20]                 ;段长度
         dec ebx                            ;段界限
         mov ecx,0x00409200                 ;字节粒度的数据段描述符
         call sys_routine_seg_sel:make_seg_descriptor   ;308行定义，用来构造段描述符，返回后EDX：EAX包含64位段描述符
         call sys_routine_seg_sel:set_up_gdt_descriptor ;263行定义，通过EDX:EAX传入描述符作为唯一的参数。该过程返回时，CX寄存器中包含了那个描述符的选择子。
         mov [edi+0x1c],cx         ;cx为set_up_gdt_descriptor的返回值，保存的段选择子

         ;建立程序堆栈段描述符
         mov ecx,[edi+0x0c]                 ;4KB的倍率  从用户程序头部偏移为0x0C的地方获得一个建议的栈大小
         mov ebx,0x000fffff ;计算栈段的界限。如果栈段的粒度是4KB，那么，用0xFFFFF减去倍率，就是用来创建描述符的段界限。
         sub ebx,ecx                        ;得到段界限
         mov eax,4096       ;第466～469行，用4096（4KB）乘以倍率，得到所需要的栈大小
         mul dword [edi+0x0c]                         
         mov ecx,eax                        ;准备为堆栈分配内存       ecx保存要分配的大小
         call sys_routine_seg_sel:allocate_memory       ;ecx中返回分配的起始地址（低地址），栈是向下增长，所以栈起始地址需要加上栈的大小
         add eax,ecx                        ;得到堆栈的高端物理地址 
         mov ecx,0x00c09600                 ;4KB粒度的堆栈段描述符
         call sys_routine_seg_sel:make_seg_descriptor   ;308行定义，用来构造段描述符，返回后EDX：EAX包含64位段描述符
         call sys_routine_seg_sel:set_up_gdt_descriptor ;263行定义，通过EDX:EAX传入描述符作为唯一的参数。该过程返回时，CX寄存器中包含了那个描述符的选择子。
         mov [edi+0x08],cx         ;cx为set_up_gdt_descriptor的返回值，保存的段选择子
       ;内核的SALT表位于338-357行
         ;重定位SALT        用DS:ESI指向C-SALT，用ES:EDI指向U-SALT。
         mov eax,[edi+0x04]        ;程序头部的段选择子给eax
         mov es,eax                         ;es -> 用户程序头部       ES指向用户程序头部段
         mov eax,core_data_seg_sel ;479、480 行，使段寄存器DS指向内核数据段。
         mov ds,eax  
      
         cld         ;将EFLAGS中的DF位置为0，使cmps指令按正向进行比较。如果DF＝1，表明是反向比较

         mov ecx,[es:0x24]                  ;用户程序的SALT条目数     存入ecx控制循环
         mov edi,0x28                       ;用户程序内的SALT位于头部内0x2c处       用于将U-SALT在头部段内的偏移量传送到EDI寄存器。
  .b2: 
         push ecx    ;内循环需要使用ecx和edi所以先压栈。487-489，512-515是外循环
         push edi    ;外循环的任务是从U-SALT中依次取出表项
      
         mov ecx,salt_items ;定义于360，即C-SALT条目的个数。重置循环次数，即C-SALT条目的个数
         mov esi,salt       ;重新使ESI寄存器指向C-SALT的开始处
  .b3:
         push edi
         push esi
         push ecx
       ;repe（repz）和repne（repnz）前缀，前者的意思是“若相等（为零）则重复”，后者的意思是“若不等（非零）则重复”。总比较次数由ECX（32）/CX（16）控制
         mov ecx,64                         ;检索表中，每条目的比较次数 ECX控制循环比较的次数，
         repe cmpsd                         ;每次比较4字节，把两个操作数相减，然后根据结果设置标志寄存器中相应的标志位。
         jnz .b4            ;DS:ESI和ES:EDI指定比较的位置，如果DF＝0，表明是正向比较。  cmpsb字节比较  cmpsw字比较  cmpsd双字比较
         mov eax,[esi]                      ;若匹配（俩字符串相等），esi恰好指向其后的地址数据    ESI寄存器正好指向C-SALT每个条目后的入口数据
         mov [es:edi-256],eax               ;将字符串改写成偏移地址 
         mov ax,[esi+4]            ;C-SALT中的每个条目是262字节，最后的6字节分别是偏移地址和段选择子。
         mov [es:edi-252],ax                ;以及段选择子 
  .b4:
      
         pop ecx
         pop esi
         add esi,salt_item_len     ;定义于359，每个条目的长度。ESI的内容加上C-SALT一个条目的长度，指向下一个C-SALT条目
         pop edi                            ;从头比较 
         loop .b3
      
         pop edi
         add edi,256        ;使edi指向下一个U-SALT条目
         pop ecx
         loop .b2

         mov ax,[es:0x04]   ;517行，把用户程序头部段的选择子传送到AX寄存器。

         pop es                             ;恢复到调用此过程前的es段 
         pop ds                             ;恢复到调用此过程前的ds段
      
         pop edi
         pop esi
         pop edx
         pop ecx
         pop ebx
      
         ret
      
;-------------------------------------------------------------------------------
start:
         mov ecx,core_data_seg_sel           ;使ds指向核心数据段 
         mov ds,ecx
       ;ds:ebx字符串的段选择子+偏移
         mov ebx,message_1  ;362行定义
         call sys_routine_seg_sel:put_string     ;37行定义的，在公共例呈段内
                                         
         ;显示处理器品牌信息 
         mov eax,0x80000002        ;EAX用于指定要返回什么样的信息，也就是功能。
         cpuid       ;用于返回处理器的标识和特性信息，处理器将返回的信息放在EAX、EBX、ECX或者EDX中。
         mov [cpu_brand + 0x00],eax       ;cpu_brand在381行声明了52个字节
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
       ;先在屏幕上留出空行，再显示处理器品牌信息，然后再留空，以突出要显示的内容。
         mov ebx,cpu_brnd0  ;380行声明的数据，用来实现回车换行
         call sys_routine_seg_sel:put_string
         mov ebx,cpu_brand  ;381行定义的数据，刚才在这块内存中已经保存了cpu相关信息
         call sys_routine_seg_sel:put_string
         mov ebx,cpu_brnd1  ;382行定义的数据，回车换行2次
         call sys_routine_seg_sel:put_string
       ;367行定义的字符串，将其显示出来
         mov ebx,message_5
         call sys_routine_seg_sel:put_string
         mov esi,50                          ;用户程序位于逻辑50扇区 
         call load_relocate_program       ;定义在387行，作用是加载和重定位用户程序。
       ;在屏幕上显示信息，表示加载和重定位工作已经完成。
         mov ebx,do_status
         call sys_routine_seg_sel:put_string
      
         mov [esp_pointer],esp               ;临时保存堆栈指针 定义于378行
       
         mov ds,ax   ;使段寄存器DS指向用户程序头部      ax在load_relocate_program中被修改为用户程序头部的段选择子
      
         jmp far [0x10]                      ;控制权交给用户程序（入口点）
                                             ;堆栈可能切换 
;当用户程序调用该过程时，意味着结束用户程序，将控制返回到内核。
return_point:                                ;用户程序返回点
         mov eax,core_data_seg_sel           ;使ds指向核心数据段
         mov ds,eax

         mov eax,core_stack_seg_sel          ;切换回内核自己的堆栈
         mov ss,eax 
         mov esp,[esp_pointer]
       ;显示一条消息，表示现在已经回到了内核。
         mov ebx,message_6
         call sys_routine_seg_sel:put_string

         ;这里可以放置清除用户程序各种描述符的指令
         ;也可以加载并启动其它程序
       
         hlt  ;使处理器进入停机状态。
            
;===============================================================================
SECTION core_trail
;-------------------------------------------------------------------------------
core_end: