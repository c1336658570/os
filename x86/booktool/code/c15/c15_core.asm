         ;代码清单15-1
         ;文件名：c15_core.asm
         ;文件说明：保护模式微型核心程序 
         ;创建日期：2011-11-19 21:40

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
allocate_memory:                            ;分配内存
                                            ;输入：ECX=希望分配的字节数
                                            ;输出：ECX=起始线性地址 
         push ds
         push eax
         push ebx
      
         mov eax,core_data_seg_sel
         mov ds,eax
      
         mov eax,[ram_alloc]
         add eax,ecx                        ;下一次分配时的起始地址
      
         ;这里应当有检测可用内存数量的指令
          
         mov ecx,[ram_alloc]                ;返回分配的起始地址

         mov ebx,eax
         and ebx,0xfffffffc
         add ebx,4                          ;强制对齐 
         test eax,0x00000003                ;下次分配的起始地址最好是4字节对齐
         cmovnz eax,ebx                     ;如果没有对齐，则强制对齐 
         mov [ram_alloc],eax                ;下次从该地址分配内存
                                            ;cmovcc指令可以避免控制转移 
         pop ebx
         pop eax
         pop ds

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
         mov es,ebx                ;让es指向整个0~4G的数据段，为了访问GDT

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
       ;构造调用门的高32位  15~0，属性，31~16，目标例呈的段内偏移的31~16
         mov edx,eax        ;复制32位偏移地址
         and edx,0xffff0000                 ;得到偏移地址高16位 
         or dx,cx                           ;组装属性部分到EDX
       ;构造调用门的低32位  15~0，段内偏移15~0，31~16,例呈所在代码段选择子
         and eax,0x0000ffff                 ;得到偏移地址低16位 
         shl ebx,16                          
         or eax,ebx                         ;组装段选择子部分
      
         pop ecx
         pop ebx
      
         retf               ;使得控制返回调用者，该过程必须以远调用的方式使用。                                  

;该过程用来结束当前任务的执行，并转换到其他任务。不要忘了，我们现在仍处在用户任务中，要结束当前的用户任务，可以先切换到程序管理器任务，然后回收用户程序所占用的内存空间，并保证不再转换到该任务。为了切换到程序管理器任务，需要根据当前任务EFLAGS寄存器的NT位决定是采用iret指令，还是jmp指令。
terminate_current_task:                     ;终止当前任务
                                            ;注意，执行此例程时，当前任务仍在
                                            ;运行中。此例程其实也是当前任务的
                                            ;一部分 
         pushfd      ;将eflags压栈 使用ESP寄存器作为指令的地址操作数时，默认使用的段寄存器是SS，即访问栈段。
         mov edx,[esp]                      ;获得EFLAGS寄存器内容     用ESP寄存器作为地址操作数访问栈，取得EFLAGS的压栈值，并传送到EDX寄存器。
         add esp,4                          ;恢复堆栈指针      将ESP寄存器的内容加上4，使栈平衡，保持压入EFLAGS寄存器前的状态。

         mov eax,core_data_seg_sel
         mov ds,eax  ;令段寄存器DS指向内核数据段，以方便后面的操作。
;DX寄存器包含了标志寄存器EFLAGS的低16位，其中，位14是NT位。第365、366行，测试DX寄存器的位14，看NT标志位是0还是1，以决定采用哪种方式（iret或者call）回到程序管理器任务。
         test dx,0100_0000_0000_0000B       ;测试NT位
         jnz .b1                            ;当前任务是嵌套的，到.b1执行iretd 
         mov ebx,core_msg1                  ;当前任务不是嵌套的，直接切换到 
         call sys_routine_seg_sel:put_string
         jmp far [prgman_tss]               ;程序管理器任务 位于431行，在那里初始化了6字节，即16位的TSS描述符选择子和32位的TSS基地址。按道理，这里不应该是TSS 基地址，而应当是一个32位偏移量。不过，这是无所谓的，当处理器看到选择子部分是一个TSS描述符选择子时，它将偏移量丢弃不用。
       
  .b1: ;因为当前任务是嵌套在程序管理器任务内的，所以NT位必然是“1”，应当转到标号.b1处继续执行。
         mov ebx,core_msg0  ;448行，是在内核数据段，用标号core_msg0声明并初始化的。
         call sys_routine_seg_sel:put_string
         iretd  ;通过iretd指令转换到前一个任务，即程序管理器任务。执行任务切换时，当前用户任务的TSS描述符的B位被清零，EFLAGS寄存器的NT位也被清零，并被保存到它的TSS中。
;我们用的是iretd，而不是iret。实际上，这是同一条指令，机器码都是CF。在16位模式下，iret 指令的操作数默认是16 位的，要按32 位操作数执行，须加指令前缀0x66，即66 CF。为了方便，编译器创造了iretd。当在16 位模式下使用iretd 时，编译器就知道，应当加上指令前缀0x66。在32 位模式下，iret 和iretd 是相同的
sys_routine_end:

;===============================================================================
SECTION core_data vstart=0                  ;系统核心的数据段 
;------------------------------------------------------------------------------- 
         pgdt             dw  0             ;用于设置和修改GDT 
                          dd  0

         ram_alloc        dd  0x00100000    ;下次分配内存时的起始地址

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

         message_1        db  '  If you seen this message,that means we '
                          db  'are now in protect mode,and the system '
                          db  'core is loaded,and the video display '
                          db  'routine works perfectly.',0x0d,0x0a,0

         message_2        db  '  System wide CALL-GATE mounted.',0x0d,0x0a,0
         
         bin_hex          db '0123456789ABCDEF'
                                            ;put_hex_dword子过程用的查找表 

         core_buf   times 2048 db 0         ;内核用的缓冲区

         cpu_brnd0        db 0x0d,0x0a,'  ',0
         cpu_brand  times 52 db 0
         cpu_brnd1        db 0x0d,0x0a,0x0d,0x0a,0

         ;任务控制块链
         tcb_chain        dd  0

         ;程序管理器的任务信息 
         prgman_tss       dd  0             ;程序管理器的TSS基地址
                          dw  0             ;程序管理器的TSS描述符选择子 

         prgman_msg1      db  0x0d,0x0a   ;方括号中显示了信息的来源，是程序管理器。后面那段话的意思是“你好！我是程序管理器，运行在0特权级上。
                          db  '[PROGRAM MANAGER]: Hello! I am Program Manager,'
                          db  'run at CPL=0.Now,create user task and switch '
                          db  'to it by the CALL instruction...',0x0d,0x0a,0
                 
         prgman_msg2      db  0x0d,0x0a
                          db  '[PROGRAM MANAGER]: I am glad to regain control.'
                          db  'Now,create another user task and switch to '
                          db  'it by the JMP instruction...',0x0d,0x0a,0
                 
         prgman_msg3      db  0x0d,0x0a
                          db  '[PROGRAM MANAGER]: I am gain control again,'
                          db  'HALT...',0

         core_msg0        db  0x0d,0x0a
                          db  '[SYSTEM CORE]: Uh...This task initiated with '
                          db  'CALL instruction or an exeception/ interrupt,'
                          db  'should use IRETD instruction to switch back...'
                          db  0x0d,0x0a,0

         core_msg1        db  0x0d,0x0a
                          db  '[SYSTEM CORE]: Uh...This task initiated with '
                          db  'JMP instruction,  should switch to Program '
                          db  'Manager directly by the JMP instruction...'
                          db  0x0d,0x0a,0

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
;和上一章相比没有太大变化，仅仅是对TSS的填写比较完整。注意，这是任务切换的要求，从一个任务切换到另一个任务时，处理器要从新任务的TSS中恢复（加载）各个寄存器的内容。
;------------------------------------------------------------------------------- 
load_relocate_program:                      ;加载并重定位用户程序
                                            ;输入: PUSH 逻辑扇区号
                                            ;      PUSH 任务控制块基地址
                                            ;输出：无 
         pushad
      
         push ds
         push es
      
         mov ebp,esp                        ;为访问通过堆栈传递的参数做准备
      
         mov ecx,mem_0_4_gb_seg_sel
         mov es,ecx
      
         mov esi,[ebp+11*4]                 ;从堆栈中取得TCB的基地址

         ;以下申请创建LDT所需要的内存
         mov ecx,160                        ;允许安装20个LDT描述符
         call sys_routine_seg_sel:allocate_memory
         mov [es:esi+0x0c],ecx              ;登记LDT基地址到TCB中
         mov word [es:esi+0x0a],0xffff      ;登记LDT初始的界限到TCB中 

         ;以下开始加载用户程序 
         mov eax,core_data_seg_sel
         mov ds,eax                         ;切换DS到内核数据段
       
         mov eax,[ebp+12*4]                 ;从堆栈中取出用户程序起始扇区号 
         mov ebx,core_buf                   ;读取程序头部数据     
         call sys_routine_seg_sel:read_hard_disk_0

         ;以下判断整个程序有多大
         mov eax,[core_buf]                 ;程序尺寸
         mov ebx,eax
         and ebx,0xfffffe00                 ;使之512字节对齐（能被512整除的数低 
         add ebx,512                        ;9位都为0 
         test eax,0x000001ff                ;程序的大小正好是512的倍数吗? 
         cmovnz eax,ebx                     ;不是。使用凑整的结果
      
         mov ecx,eax                        ;实际需要申请的内存数量
         call sys_routine_seg_sel:allocate_memory
         mov [es:esi+0x06],ecx              ;登记程序加载基地址到TCB中
      
         mov ebx,ecx                        ;ebx -> 申请到的内存首地址
         xor edx,edx
         mov ecx,512
         div ecx
         mov ecx,eax                        ;总扇区数 
      
         mov eax,mem_0_4_gb_seg_sel         ;切换DS到0-4GB的段
         mov ds,eax

         mov eax,[ebp+12*4]                 ;起始扇区号 
  .b1:
         call sys_routine_seg_sel:read_hard_disk_0
         inc eax
         loop .b1                           ;循环读，直到读完整个用户程序

         mov edi,[es:esi+0x06]              ;获得程序加载基地址

         ;建立程序头部段描述符
         mov eax,edi                        ;程序头部起始线性地址
         mov ebx,[edi+0x04]                 ;段长度
         dec ebx                            ;段界限
         mov ecx,0x0040f200                 ;字节粒度的数据段描述符，特权级3 
         call sys_routine_seg_sel:make_seg_descriptor
      
         ;安装头部段描述符到LDT中 
         mov ebx,esi                        ;TCB的基地址
         call fill_descriptor_in_ldt

         or cx,0000_0000_0000_0011B         ;设置选择子的特权级为3
         mov [es:esi+0x44],cx               ;登记程序头部段选择子到TCB 
         mov [edi+0x04],cx                  ;和头部内 
      
         ;建立程序代码段描述符
         mov eax,edi
         add eax,[edi+0x14]                 ;代码起始线性地址
         mov ebx,[edi+0x18]                 ;段长度
         dec ebx                            ;段界限
         mov ecx,0x0040f800                 ;字节粒度的代码段描述符，特权级3
         call sys_routine_seg_sel:make_seg_descriptor
         mov ebx,esi                        ;TCB的基地址
         call fill_descriptor_in_ldt
         or cx,0000_0000_0000_0011B         ;设置选择子的特权级为3
         mov [edi+0x14],cx                  ;登记代码段选择子到头部

         ;建立程序数据段描述符
         mov eax,edi
         add eax,[edi+0x1c]                 ;数据段起始线性地址
         mov ebx,[edi+0x20]                 ;段长度
         dec ebx                            ;段界限 
         mov ecx,0x0040f200                 ;字节粒度的数据段描述符，特权级3
         call sys_routine_seg_sel:make_seg_descriptor
         mov ebx,esi                        ;TCB的基地址
         call fill_descriptor_in_ldt
         or cx,0000_0000_0000_0011B         ;设置选择子的特权级为3
         mov [edi+0x1c],cx                  ;登记数据段选择子到头部

         ;建立程序堆栈段描述符
         mov ecx,[edi+0x0c]                 ;4KB的倍率 
         mov ebx,0x000fffff
         sub ebx,ecx                        ;得到段界限
         mov eax,4096                        
         mul ecx                         
         mov ecx,eax                        ;准备为堆栈分配内存 
         call sys_routine_seg_sel:allocate_memory
         add eax,ecx                        ;得到堆栈的高端物理地址 
         mov ecx,0x00c0f600                 ;字节粒度的堆栈段描述符，特权级3
         call sys_routine_seg_sel:make_seg_descriptor
         mov ebx,esi                        ;TCB的基地址
         call fill_descriptor_in_ldt
         or cx,0000_0000_0000_0011B         ;设置选择子的特权级为3
         mov [edi+0x08],cx                  ;登记堆栈段选择子到头部

         ;重定位SALT 
         mov eax,mem_0_4_gb_seg_sel         ;这里和前一章不同，头部段描述符
         mov es,eax                         ;已安装，但还没有生效，故只能通
                                            ;过4GB段访问用户程序头部          
         mov eax,core_data_seg_sel
         mov ds,eax
      
         cld

         mov ecx,[es:edi+0x24]              ;U-SALT条目数(通过访问4GB段取得) 
         add edi,0x28                       ;U-SALT在4GB段内的偏移 
  .b2: 
         push ecx
         push edi
      
         mov ecx,salt_items
         mov esi,salt
  .b3:
         push edi
         push esi
         push ecx

         mov ecx,64                         ;检索表中，每条目的比较次数 
         repe cmpsd                         ;每次比较4字节 
         jnz .b4
         mov eax,[esi]                      ;若匹配，则esi恰好指向其后的地址
         mov [es:edi-256],eax               ;将字符串改写成偏移地址 
         mov ax,[esi+4]
         or ax,0000000000000011B            ;以用户程序自己的特权级使用调用门
                                            ;故RPL=3 
         mov [es:edi-252],ax                ;回填调用门选择子 
  .b4:
      
         pop ecx
         pop esi
         add esi,salt_item_len
         pop edi                            ;从头比较 
         loop .b3
      
         pop edi
         add edi,256
         pop ecx
         loop .b2

         mov esi,[ebp+11*4]                 ;从堆栈中取得TCB的基地址

         ;创建0特权级堆栈
         mov ecx,4096
         mov eax,ecx                        ;为生成堆栈高端地址做准备 
         mov [es:esi+0x1a],ecx
         shr dword [es:esi+0x1a],12         ;登记0特权级堆栈尺寸到TCB 
         call sys_routine_seg_sel:allocate_memory
         add eax,ecx                        ;堆栈必须使用高端地址为基地址
         mov [es:esi+0x1e],eax              ;登记0特权级堆栈基地址到TCB 
         mov ebx,0xffffe                    ;段长度（界限）
         mov ecx,0x00c09600                 ;4KB粒度，读写，特权级0
         call sys_routine_seg_sel:make_seg_descriptor
         mov ebx,esi                        ;TCB的基地址
         call fill_descriptor_in_ldt
         ;or cx,0000_0000_0000_0000          ;设置选择子的特权级为0
         mov [es:esi+0x22],cx               ;登记0特权级堆栈选择子到TCB
         mov dword [es:esi+0x24],0          ;登记0特权级堆栈初始ESP到TCB
      
         ;创建1特权级堆栈
         mov ecx,4096
         mov eax,ecx                        ;为生成堆栈高端地址做准备
         mov [es:esi+0x28],ecx
         shr dword [es:esi+0x28],12               ;登记1特权级堆栈尺寸到TCB
         call sys_routine_seg_sel:allocate_memory
         add eax,ecx                        ;堆栈必须使用高端地址为基地址
         mov [es:esi+0x2c],eax              ;登记1特权级堆栈基地址到TCB
         mov ebx,0xffffe                    ;段长度（界限）
         mov ecx,0x00c0b600                 ;4KB粒度，读写，特权级1
         call sys_routine_seg_sel:make_seg_descriptor
         mov ebx,esi                        ;TCB的基地址
         call fill_descriptor_in_ldt
         or cx,0000_0000_0000_0001          ;设置选择子的特权级为1
         mov [es:esi+0x30],cx               ;登记1特权级堆栈选择子到TCB
         mov dword [es:esi+0x32],0          ;登记1特权级堆栈初始ESP到TCB

         ;创建2特权级堆栈
         mov ecx,4096
         mov eax,ecx                        ;为生成堆栈高端地址做准备
         mov [es:esi+0x36],ecx
         shr dword [es:esi+0x36],12               ;登记2特权级堆栈尺寸到TCB
         call sys_routine_seg_sel:allocate_memory
         add eax,ecx                        ;堆栈必须使用高端地址为基地址
         mov [es:esi+0x3a],ecx              ;登记2特权级堆栈基地址到TCB
         mov ebx,0xffffe                    ;段长度（界限）
         mov ecx,0x00c0d600                 ;4KB粒度，读写，特权级2
         call sys_routine_seg_sel:make_seg_descriptor
         mov ebx,esi                        ;TCB的基地址
         call fill_descriptor_in_ldt
         or cx,0000_0000_0000_0010          ;设置选择子的特权级为2
         mov [es:esi+0x3e],cx               ;登记2特权级堆栈选择子到TCB
         mov dword [es:esi+0x40],0          ;登记2特权级堆栈初始ESP到TCB
      
         ;在GDT中登记LDT描述符
         mov eax,[es:esi+0x0c]              ;LDT的起始线性地址
         movzx ebx,word [es:esi+0x0a]       ;LDT段界限
         mov ecx,0x00408200                 ;LDT描述符，特权级0
         call sys_routine_seg_sel:make_seg_descriptor
         call sys_routine_seg_sel:set_up_gdt_descriptor
         mov [es:esi+0x10],cx               ;登记LDT选择子到TCB中
       
         ;创建用户程序的TSS
         mov ecx,104                        ;tss的基本尺寸
         mov [es:esi+0x12],cx              
         dec word [es:esi+0x12]             ;登记TSS界限值到TCB 
         call sys_routine_seg_sel:allocate_memory
         mov [es:esi+0x14],ecx              ;登记TSS基地址到TCB
      
         ;登记基本的TSS表格内容
         mov word [es:ecx+0],0              ;反向链=0
      
         mov edx,[es:esi+0x24]              ;登记0特权级堆栈初始ESP
         mov [es:ecx+4],edx                 ;到TSS中
      
         mov dx,[es:esi+0x22]               ;登记0特权级堆栈段选择子
         mov [es:ecx+8],dx                  ;到TSS中
      
         mov edx,[es:esi+0x32]              ;登记1特权级堆栈初始ESP
         mov [es:ecx+12],edx                ;到TSS中

         mov dx,[es:esi+0x30]               ;登记1特权级堆栈段选择子
         mov [es:ecx+16],dx                 ;到TSS中

         mov edx,[es:esi+0x40]              ;登记2特权级堆栈初始ESP
         mov [es:ecx+20],edx                ;到TSS中

         mov dx,[es:esi+0x3e]               ;登记2特权级堆栈段选择子
         mov [es:ecx+24],dx                 ;到TSS中

         mov dx,[es:esi+0x10]               ;登记任务的LDT选择子
         mov [es:ecx+96],dx                 ;到TSS中
      
         mov dx,[es:esi+0x12]               ;登记任务的I/O位图偏移
         mov [es:ecx+102],dx                ;到TSS中 
         
         mov word [es:ecx+100],0            ;T=0
      
         mov dword [es:ecx+28],0            ;登记CR3(PDBR)
;766-790，比起上一章，新增加的内容
         ;访问用户程序头部，获取数据填充TSS  从栈中取出TCB的基地址；然后，通过4GB的内存段访问TCB，取出用户程序加载的起始地址，这也是用户程序头部的起始地址。
         mov ebx,[ebp+11*4]                 ;从堆栈中取得TCB的基地址
         mov edi,[es:ebx+0x06]              ;用户程序加载的基地址，这也是用户程序头部的起始地址。
;因为这是用户程序的第一次执行，所以，TSS中的EIP域应该登记用户程序的入口点，CS域应该登记用户程序入口点所在的代码段选择子。
         mov edx,[es:edi+0x10]              ;登记程序入口点（EIP） 
         mov [es:ecx+32],edx                ;到TSS

         mov dx,[es:edi+0x14]               ;登记程序代码段（CS）选择子
         mov [es:ecx+76],dx                 ;到TSS中

         mov dx,[es:edi+0x08]               ;登记程序堆栈段（SS）选择子
         mov [es:ecx+80],dx                 ;到TSS中

         mov dx,[es:edi+0x04]               ;登记程序数据段（DS）选择子
         mov word [es:ecx+84],dx            ;到TSS中。注意，它指向程序头部段
      
         mov word [es:ecx+72],0             ;TSS中的ES=0

         mov word [es:ecx+88],0             ;TSS中的FS=0

         mov word [es:ecx+92],0             ;TSS中的GS=0
;第787～790行，先将EFLAGS寄存器的内容压入栈，再将其弹出到EDX寄存器，因为不存在将标志寄存器的内容完整地传送到通用寄存器的指令。接着，把EDX中的内容写入TSS中EFLAGS域。注意，这是当前任务（程序管理器）EFLAGS寄存器的副本，新任务将使用这个副本作为初始的EFLAGS。一般来说，此时EFLAGS寄存器的IOPL字段为00，将来新任务开始执行时，会用这个副本作为处理器EFLAGS寄存器的当前值，并因此而没有足够的I/O特权。
         pushfd
         pop edx
;把EDX中的内容写入TSS中EFLAGS域。注意，这是当前任务（程序管理器）EFLAGS寄存器的副本，新任务将使用这个副本作为初始的EFLAGS。一般来说，此时EFLAGS寄存器的IOPL字段为00，将来新任务开始执行时，会用这个副本作为处理器EFLAGS寄存器的当前值，并因此而没有足够的I/O特权。         
         mov dword [es:ecx+36],edx          ;EFLAGS     

         ;在GDT中登记TSS描述符
         mov eax,[es:esi+0x14]              ;TSS的起始线性地址
         movzx ebx,word [es:esi+0x12]       ;段长度（界限）
         mov ecx,0x00408900                 ;TSS描述符，特权级0
         call sys_routine_seg_sel:make_seg_descriptor
         call sys_routine_seg_sel:set_up_gdt_descriptor
         mov [es:esi+0x18],cx               ;登记TSS选择子到TCB

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
start:   ;848->906和上一章相同，要是显示处理器品牌信息，以及安装供每个任务使用的调用门。
         mov ecx,core_data_seg_sel          ;令DS指向核心数据段 
         mov ds,ecx

         mov ecx,mem_0_4_gb_seg_sel         ;令ES指向4GB数据段 
         mov es,ecx

         mov ebx,message_1                    
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

         ;以下开始安装为整个系统服务的调用门。特权级之间的控制转移必须使用门
         mov edi,salt                       ;C-SALT表的起始位置 
         mov ecx,salt_items                 ;C-SALT表的条目数量 
  .b3:
         push ecx   
         mov eax,[edi+256]                  ;该条目入口点的32位偏移地址 
         mov bx,[edi+260]                   ;该条目入口点的段选择子 
         mov cx,1_11_0_1100_000_00000B      ;特权级3的调用门(3以上的特权级才
;CX存调用门属性P，DPL，0，TYPE,000，参数个数0~4位;允许访问)，0个参数(因为用寄存器传递参数，而没有用栈) 
;3个参数，eax，bx和cx，TYPE1100表示调用门
         call sys_routine_seg_sel:make_gate_descriptor  ;331行，构造调用门描述符
         call sys_routine_seg_sel:set_up_gdt_descriptor ;在GDT中创建调用门描述符 返回调用门描述符选择子，RPL为0 264行
         mov [edi+260],cx                   ;将返回的门描述符选择子回填  覆盖原先的代码段选择子
         add edi,salt_item_len              ;指向下一个C-SALT条目 
         pop ecx
         loop .b3

         ;对门进行测试 
         mov ebx,message_2
         call far [salt_1+256]              ;通过门显示信息(偏移量将被忽略) 
;接下来的工作是创建0特权级的内核任务，并将当前正在执行的内核代码段划归该任务。当前代码的作用是创建其他任务，管理它们，所以称做任务管理器，或者叫程序管理器。
         ;为程序管理器的TSS分配内存空间 
         mov ecx,104                        ;为该任务的TSS分配内存
         call sys_routine_seg_sel:allocate_memory
         mov [prgman_tss+0x00],ecx          ;保存程序管理器的TSS基地址 431行，6字节，为了追踪程序管理器的TSS，需要保存它的基地址和选择子，前32位用于保存TSS的基地址，后16位则是它的选择子。
      
         ;在程序管理器的TSS中设置必要的项目 
         mov word [es:ecx+96],0             ;没有LDT。处理器允许没有LDT的任务。程序管理器可以将自己所使用的段描述符安装在GDT中。
         mov word [es:ecx+102],103          ;没有I/O位图。0特权级事实上不需要。0特权级访问硬件会被允许，不需要I/O位图。
         mov word [es:ecx+0],0              ;反向链=0 TSS内偏移0处是前一个任务的TSS描述符选择子。将指向前一个任务的指针（任务链接域）0x00-0x01填写为0，表明这是唯一的任务。
         mov dword [es:ecx+28],0            ;登记CR3(PDBR)
         mov word [es:ecx+100],0            ;T=0
                                            ;不需要0、1、2特权级堆栈。0特级不
                                            ;会向低特权级转移控制。
         
         ;创建TSS描述符，并安装到GDT中 
         mov eax,ecx                        ;TSS的起始线性地址
         mov ebx,103                        ;段长度（界限）
         mov ecx,0x00408900                 ;TSS描述符，特权级0
         call sys_routine_seg_sel:make_seg_descriptor   ;构造TSS描述符，在edx:eax中返回
         call sys_routine_seg_sel:set_up_gdt_descriptor ;将其安装到gdt中，在cx返回段选择子
         mov [prgman_tss+0x04],cx           ;保存程序管理器的TSS描述符选择子 第431行，声明并初始化了6字节的空间，前32位用于保存TSS的基地址，后16位则是它的选择子。

         ;任务寄存器TR中的内容是任务存在的标志，该内容也决定了当前任务是谁。
         ;下面的指令为当前正在执行的0特权级任务“程序管理器”后补手续（TSS）。
         ltr cx ;执行这条指令后，处理器用该选择子访问GDT，找到相对应的TSS描述符，将其B位置“1”，表示该任务正在执行中（或者处于挂起状态）。同时，还要将该描述符传送到TR寄存器的描述符高速缓存器中。

         ;现在可认为“程序管理器”任务正执行中
         mov ebx,prgman_msg1       ;显示一条信息，434行定义
         call sys_routine_seg_sel:put_string
;第938～945行是用来加载用户程序的。先分配一个任务控制块（TCB），然后将它挂到TCB链上。接着，压入用户程序的起始逻辑扇区号及其TCB基地址，作为参数调用过程load_relocate_program。
         mov ecx,0x46       ;分配TCB空间
         call sys_routine_seg_sel:allocate_memory
         call append_to_tcb_link            ;将此TCB添加到TCB链中 
      
         push dword 50                      ;用户程序位于逻辑50扇区
         push ecx                           ;压入任务控制块起始线性地址 
       
         call load_relocate_program         ;加载用户程序
;TCB的0x14-0x17保存TSS基地址，0x18-0x19保存TSS选择子，通过TSS选择子，切换任务。当处理器发现得到的是一个TSS选择子，就执行任务切换。和通过调用门的控制转移一样，32位偏移部分丢弃不用。  
         call far [es:ecx+0x14]             ;执行任务切换。和上一章不同，任务切
                                            ;换时要恢复TSS内容，所以在创建任务
                                            ;时TSS要填写完整 
;执行完call far [es:ecx+0x14]就会进入用户程序执行
         ;重新加载并切换任务       从用户任务返回会回到这里
         mov ebx,prgman_msg2       ;显示一些信息，标号位于439行
         call sys_routine_seg_sel:put_string
;对于刚刚被挂起的那个旧任务，如果它没有被终止执行，则可以不予理会，并在下一个适当的时机再次切换到它那里执行。不过，现在的情况是它希望自己被终止。所以，理论上，接下来的工作是回收它所占用的内存空间，并从任务控制块TCB 链上去掉，以确保不会再切换到该任务执行（当然，现在TCB 链还没有体现出自己的用处）。遗憾的是，我们并没有提供这样的代码。所以，这个任务将一直存在，一直有效，不会消失，在整个系统的运行期间可以随时切换过去。
         mov ecx,0x46       ;为TCB分配空间       我们再创建一个新任务，并转移到该任务执行。
         call sys_routine_seg_sel:allocate_memory
         call append_to_tcb_link            ;将此TCB添加到TCB链中

         push dword 50                      ;用户程序位于逻辑50扇区
         push ecx                           ;压入任务控制块起始线性地址

         call load_relocate_program

         jmp far [es:ecx+0x14]              ;执行任务切换
;显示一条消息，然后停机
         mov ebx,prgman_msg3              
         call sys_routine_seg_sel:put_string

         hlt
            
core_code_end:

;-------------------------------------------------------------------------------
SECTION core_trail
;-------------------------------------------------------------------------------
core_end: