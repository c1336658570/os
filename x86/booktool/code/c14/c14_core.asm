         ;代码清单14-1      修改了665行代码，在645和662添加了dword
         ;文件名：c14_core.asm
         ;文件说明：保护模式微型核心程序 
         ;创建日期：2011-11-6 18:37

         ;以下常量定义部分。内核的大部分内容都应当固定 RPL（请求特权级）都是0，0-1位。TI=0，2位，段描述符在GDT中
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

         mov ebx,core_data_seg_sel          ;切换到核心数据段  为了访问spdt那块内存
         mov ds,ebx

         sgdt [pgdt]                        ;以便开始处理GDT

         mov ebx,mem_0_4_gb_seg_sel
         mov es,ebx         ;让es指向整个0~4G的数据段，为了访问GDT

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
       ;构造调用门的高32位  15~0，属性，31~16，段内偏移31~16
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
                          dd  return_point
                          dw  core_code_seg_sel

         salt_item_len   equ $-salt_4
         salt_items      equ ($-salt)/salt_item_len

         message_1        db  '  If you seen this message,that means we '
                          db  'are now in protect mode,and the system '
                          db  'core is loaded,and the video display '
                          db  'routine works perfectly.',0x0d,0x0a,0

         message_2        db  '  System wide CALL-GATE mounted.',0x0d,0x0a,0
         
         message_3        db  0x0d,0x0a,'  Loading user program...',0
         
         do_status        db  'Done.',0x0d,0x0a,0
         
         message_6        db  0x0d,0x0a,0x0d,0x0a,0x0d,0x0a
                          db  '  User program terminated,control returned.',0

         bin_hex          db '0123456789ABCDEF'
                                            ;put_hex_dword子过程用的查找表 

         core_buf   times 2048 db 0         ;内核用的缓冲区

         esp_pointer      dd 0              ;内核用来临时保存自己的栈指针     

         cpu_brnd0        db 0x0d,0x0a,'  ',0
         cpu_brand  times 52 db 0
         cpu_brnd1        db 0x0d,0x0a,0x0d,0x0a,0

         ;任务控制块链
         tcb_chain        dd  0    ;指向第一个任务的TCB线性基地址

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

         mov ecx,mem_0_4_gb_seg_sel       ;令ds指向4G数据段
         mov ds,ecx

         mov edi,[ebx+0x0c]                 ;获得LDT基地址
       ;435～440 行，计算用于安装新描述符的线性地址，并把它安装到那里
         xor ecx,ecx
         mov cx,[ebx+0x0a]                  ;获得LDT界限
         inc cx                             ;LDT的总字节数，即新描述符偏移地址
       ;只能使用其16位的CX部分。第一次在LDT中安装描述符时，LDT的界限值是0xFFFF，加1之后，总大小是0x0000，进位部分要丢弃
         mov [edi+ecx+0x00],eax
         mov [edi+ecx+0x04],edx             ;安装描述符
       ;将LDT的总大小（字节数）在原来的基础上增加8字节，再减去1，就是新的界限值
         add cx,8                           
         dec cx                             ;得到新的LDT界限值 

         mov [ebx+0x0a],cx                  ;更新LDT界限值到TCB
       ;将描述符的界限值除以8，余数丢弃不管，所得的商就是当前新描述符的索引号
         mov ax,cx
         xor dx,dx
         mov cx,8
         div cx
       ;将CX寄存器中的索引号逻辑左移3次，并将TI位置1，表示指向LDT，这就得到了当前描述符的选择子。
         mov cx,ax
         shl cx,3                           ;左移3位，并且
         or cx,0000_0000_0000_0100B         ;使TI位=1，指向LDT，最后使RPL=00 
       ;用CX寄存器返回一个选择子。
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
         pushad      ;压入8个通用寄存器
      
         push ds     ;push默认是双字，ds压入时会进行0拓展，然后出栈时会截断
         push es
       ;将esp栈顶寄存器的值给ebp，ebp访问内存时默认使用段寄存器SS，SS:EBP
         mov ebp,esp                        ;为访问通过堆栈传递的参数做准备
      
         mov ecx,mem_0_4_gb_seg_sel
         mov es,ecx
       ;使用段寄存器SS描述符高速缓存器中的32位基地址，加上EBP提供的32位偏移量，加立即数，形成32位线性地址
         mov esi,[ebp+11*4]                 ;从堆栈中取得TCB的基地址
       ;11*4，因为压入ds，es和8个通用寄存器，加EIP（返回时指令的偏移地址），因为是相对近调用，所以只压入EIP
         ;以下申请创建LDT所需要的内存
         mov ecx,160 ;分配160字节内存，供LDT使用;允许安装20个LDT描述符
         call sys_routine_seg_sel:allocate_memory  ;分配内存
         mov [es:esi+0x0c],ecx              ;登记LDT基地址到TCB中 将LDT的起始线性地址登记在任务控制块TCB中
         mov word [es:esi+0x0a],0xffff      ;登记LDT初始的界限到TCB中  将LDT的大小登记在任务控制块TCB中
       ;因为我们刚创建LDT，总字节数为0，所以，当前的界限值应当是0xFFFF（0减去1）。
         ;以下开始加载用户程序 
         mov eax,core_data_seg_sel 
         mov ds,eax                         ;切换DS到内核数据段
       ;通过ss:ebp+12*4取出逻辑扇区号
         mov eax,[ebp+12*4]                 ;从堆栈中取出用户程序起始扇区号 
         mov ebx,core_buf                   ;读取程序头部数据     
         call sys_routine_seg_sel:read_hard_disk_0 ;145行，将程序头部读入内核数据段core_buf中
       
         ;以下判断整个程序有多大
         mov eax,[core_buf]                 ;程序尺寸
         mov ebx,eax
         and ebx,0xfffffe00                 ;使之512字节对齐（能被512整除的数低 
         add ebx,512                        ;9位都为0 
         test eax,0x000001ff                ;程序的大小正好是512的倍数吗? 
         cmovnz eax,ebx                     ;不是。使用凑整的结果
      
         mov ecx,eax                        ;实际需要申请的内存数量
         call sys_routine_seg_sel:allocate_memory  ;申请内存 ecx为要申请的内存字节数
         mov [es:esi+0x06],ecx              ;登记程序加载基地址到TCB中
       ;计算总扇区数
         mov ebx,ecx                        ;ebx -> 申请到的内存首地址
         xor edx,edx
         mov ecx,512
         div ecx
         mov ecx,eax                        ;总扇区数 
      
         mov eax,mem_0_4_gb_seg_sel         ;切换DS到0-4GB的段
         mov ds,eax

         mov eax,[ebp+12*4]                 ;起始扇区号 
  .b1: ;循环将用户程序加载到内存中
         call sys_routine_seg_sel:read_hard_disk_0
         inc eax
         loop .b1                           ;循环读，直到读完整个用户程序

         mov edi,[es:esi+0x06]              ;从TCB中获得程序加载基地址
       ;在478行，ESI寄存器指向了TCB的基地址
         ;建立程序头部段描述符
         mov eax,edi                        ;程序头部起始线性地址
         mov ebx,[edi+0x04]                 ;段长度
         dec ebx                            ;段界限
         mov ecx,0x0040f200                 ;字节粒度的数据段描述符，特权级3 
         call sys_routine_seg_sel:make_seg_descriptor ;309行
       ;make_seg_descriptor在EDX:EAX中返回64位段描述符
         ;安装头部段描述符到LDT中 
         mov ebx,esi                        ;TCB的基地址
         call fill_descriptor_in_ldt  ;421行，把刚才创建的描述符安装到LDT中
       ;在cx中返回选择子，但是返回的选择子的最后两位为0,表明请求特权级为0，所以需要修改请求特权级为3
         or cx,0000_0000_0000_0011B         ;设置选择子的特权级为3 设置RPL（请求特权级）
         mov [es:esi+0x44],cx               ;登记程序头部段选择子到TCB 
         mov [edi+0x04],cx                  ;和头部内 将头部段选择子填到用户程序头部 
      
         ;建立程序代码段描述符
         mov eax,edi
         add eax,[edi+0x14]                 ;代码起始线性地址
         mov ebx,[edi+0x18]                 ;段长度
         dec ebx                            ;段界限
         mov ecx,0x0040f800                 ;字节粒度的代码段描述符，特权级3
         call sys_routine_seg_sel:make_seg_descriptor
         mov ebx,esi                        ;TCB的基地址
         call fill_descriptor_in_ldt ;在cx中返回选择子，但是返回的选择子的最后两位为0,表明请求特权级为0，所以需要修改请求特权级为3
         or cx,0000_0000_0000_0011B         ;设置选择子的特权级为3 设置RPL（请求特权级）
         mov [edi+0x14],cx                  ;登记代码段选择子到头部  将代码段选择子填到用户程序头部 

         ;建立程序数据段描述符
         mov eax,edi
         add eax,[edi+0x1c]                 ;数据段起始线性地址
         mov ebx,[edi+0x20]                 ;段长度
         dec ebx                            ;段界限 
         mov ecx,0x0040f200                 ;字节粒度的数据段描述符，特权级3
         call sys_routine_seg_sel:make_seg_descriptor
         mov ebx,esi                        ;TCB的基地址
         call fill_descriptor_in_ldt ;在cx中返回选择子，但是返回的选择子的最后两位为0,表明请求特权级为0，所以需要修改请求特权级为3
         or cx,0000_0000_0000_0011B         ;设置选择子的特权级为3  设置RPL（请求特权级）
         mov [edi+0x1c],cx                  ;登记数据段选择子到头部 将数据段选择子填到用户程序头部

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
         call fill_descriptor_in_ldt ;在cx中返回选择子，但是返回的选择子的最后两位为0,表明请求特权级为0，所以需要修改请求特权级为3
         or cx,0000_0000_0000_0011B         ;设置选择子的特权级为3 设置RPL（请求特权级）
         mov [edi+0x08],cx                  ;登记堆栈段选择子到头部 将堆栈段选择子填到用户程序头部 

         ;重定位SALT 579~620行  用DS:ESI指向C-SALT，用ES:EDI指向U-SALT。
         mov eax,mem_0_4_gb_seg_sel         ;这里和前一章不同，头部段描述符
         mov es,eax                         ;已安装，但还没有生效(没有加载局部描述符表寄存器LDTR)，
                                            ;故只能通过4GB段访问用户程序头部          
         mov eax,core_data_seg_sel
         mov ds,eax
      
         cld
       ;edi一直存的是用户程序的线性地址
         mov ecx,[es:edi+0x24]              ;U-SALT条目数(通过访问4GB段取得) 
         add edi,0x28                       ;U-SALT在4GB段内的偏移 
  .b2: 
         push ecx    ;ecx还需要控制内层循环，所以先入栈
         push edi    ;edi在循环中会被更改
      
         mov ecx,salt_items        ;SALT的个数
         mov esi,salt              ;SALT的起始地址
  .b3: ;循环过程cmpsd会更改esi和edi，然后ecx需要在循环中用到
         push edi
         push esi
         push ecx
;当初，在创建这些调用门时，选择子的RPL字段是0。当它们被复制到U-SALT中时，应当改为用户程序的特权级（3）。
         mov ecx,64                         ;检索表中，每条目的比较次数 
         repe cmpsd                         ;每次比较4字节 ESI和EDI每次都会+4
         jnz .b4            ;不等于0就跳到b4准备开始下次循环
         mov eax,[esi]                      ;若匹配，则esi恰好指向其后的地址
         mov [es:edi-256],eax               ;将字符串改写成调用门的偏移地址 4字节
         mov ax,[esi+4]     ;段选择子给ax  2字节
         or ax,0000000000000011B            ;以用户程序自己的特权级使用调用门
                                            ;故RPL=3 
         mov [es:edi-252],ax                ;回填调用门选择子 
  .b4:
      
         pop ecx
         pop esi
         add esi,salt_item_len     ;让esi指向C-SALT的下一项，所以需要加上C-SALT的长度
         pop edi                            ;从头比较 
         loop .b3
      
         pop edi
         add edi,256        ;让edi指向U-SALT的下一项
         pop ecx
         loop .b2
       ;执行到此处不能继续执行，bug未知
         mov esi,[ebp+11*4]                 ;从堆栈中取得TCB的基地址
       ;通过调用门的控制转移通常会改变当前特权级CPL，jmp不会改变，call会改变，同时还要切换到与目标代码段特权级相同的栈。
         ;创建0特权级堆栈  对于当前的3特权级任务来说，应当创建特权级0、1和2的栈。而且，应当将它们定义在每个任务自己的LDT中。这些额外的栈是动态创建的，而且需要登记在任务状态段（TSS）中，以便处理器固件能够自动访问到它们。但是，现在的问题是还没有创建TSS，有必要先将这些栈信息登记在任务控制块（TCB）中暂时保存。
         mov ecx,4096
         mov eax,ecx                        ;为生成堆栈高端地址做准备 
         mov [es:esi+0x1a],ecx     ;TCB的0x1a-0x1d存0特权级栈以4KB为单位的栈的长度
         shr dword [es:esi+0x1a],12         ;登记0特权级堆栈尺寸到TCB 右移12位得到4K的倍数
         call sys_routine_seg_sel:allocate_memory  ;分配内存，ecx存要分配的内存字节数，返回分配的内存的起始地址到ecx中
         add eax,ecx                        ;堆栈必须使用高端地址为基地址
         mov [es:esi+0x1e],eax              ;登记0特权级堆栈基地址到TCB 0x1e-0x21保存0特权级堆栈基地址
         mov ebx,0xffffe                    ;段长度（界限）
         mov ecx,0x00c09600                 ;4KB粒度，读写，特权级0
         call sys_routine_seg_sel:make_seg_descriptor   ;309行 创建描述符 返回edx:eax
         mov ebx,esi                        ;TCB的基地址 -> ebx
         call fill_descriptor_in_ldt      ;将其加载到ldt中，ebx存TCB基地址，edx:eax存描述符，cx返回段选择子，返回的选择子的RPL为0
         ;or cx,0000_0000_0000_0000          ;设置选择子的特权级为0 上面那个函数返回的选择子本来就是RPL为0的
         mov [es:esi+0x22],cx               ;登记0特权级堆栈选择子到TCB 0x22-0x23保存0特权级栈选择子
         mov dword [es:esi+0x24],0          ;登记0特权级堆栈初始ESP到TCB 0x24-0x27存0特权级初始esp
      
         ;创建1特权级堆栈
         mov ecx,4096
         mov eax,ecx                        ;为生成堆栈高端地址做准备
         mov [es:esi+0x28],ecx    ;TCB的0x28-0x2b存1特权级栈以4KB为单位的栈的长度
         shr dword [es:esi+0x28],12               ;登记1特权级堆栈尺寸到TCB 右移12位得到4K的倍数
         call sys_routine_seg_sel:allocate_memory ;分配内存，ecx存要分配的内存字节数，返回分配的内存的起始地址到ecx中
         add eax,ecx                        ;堆栈必须使用高端地址为基地址
         mov [es:esi+0x2c],eax              ;登记1特权级堆栈基地址到TCB 0x2c-0x2f保存1特权级堆栈基地址
         mov ebx,0xffffe                    ;段长度（界限）
         mov ecx,0x00c0b600                 ;4KB粒度，读写，特权级1
         call sys_routine_seg_sel:make_seg_descriptor   ;309行 创建描述符 返回edx:eax
         mov ebx,esi                        ;TCB的基地址 -> ebx
         call fill_descriptor_in_ldt      ;将其加载到ldt中，ebx存TCB基地址，edx:eax存描述符，cx返回段选择子，返回的选择子的RPL为0
         or cx,0000_0000_0000_0001          ;设置选择子的特权级为1  因为这个栈是从低特权级切换到1特权级时使用的栈，所以DPL为1
         mov [es:esi+0x30],cx               ;登记1特权级堆栈选择子到TCB 0x30-0x31保存1特权级栈选择子
         mov dword [es:esi+0x32],0          ;登记1特权级堆栈初始ESP到TCB 0x32-0x35存1特权级初始esp

         ;创建2特权级堆栈
         mov ecx,4096
         mov eax,ecx                        ;为生成堆栈高端地址做准备
         mov [es:esi+0x36],ecx     ;TCB的0x36-0x39存2特权级栈以4KB为单位的栈的长度
         shr dword [es:esi+0x36],12               ;登记2特权级堆栈尺寸到TCB 右移12位得到4K的倍数
         call sys_routine_seg_sel:allocate_memory ;分配内存，ecx存要分配的内存字节数，返回分配的内存的起始地址到ecx中
         add eax,ecx                        ;堆栈必须使用高端地址为基地址
         mov [es:esi+0x3a],eax ;原书此处为ecx ;登记2特权级堆栈基地址到TCB 0x3a-0x3d保存2特权级堆栈基地址
         mov ebx,0xffffe                    ;段长度（界限）
         mov ecx,0x00c0d600                 ;4KB粒度，读写，特权级2
         call sys_routine_seg_sel:make_seg_descriptor   ;309行 创建描述符 返回edx:eax
         mov ebx,esi                        ;TCB的基地址 -> ebx
         call fill_descriptor_in_ldt      ;将其加载到ldt中，ebx存TCB基地址，edx:eax存描述符，cx返回段选择子，返回的选择子的RPL为0
         or cx,0000_0000_0000_0010          ;设置选择子的特权级为2  因为这个栈是从低特权级切换到2特权级时使用的栈，所以DPL为2
         mov [es:esi+0x3e],cx               ;登记2特权级堆栈选择子到TCB 0x3e-0x3f保存2特权级栈选择子
         mov dword [es:esi+0x40],0          ;登记2特权级堆栈初始ESP到TCB 0x40-0x43存2特权级初始esp
;处理器要求在GDT中安装每个LDT的描述符。当要使用这些LDT时，可以用它们的选择子来访问GDT，将LDT描述符加载到LDTR寄存器。
         ;在GDT中登记LDT描述符 在S＝0的前提下，TYPE字段为0010（二进制）表明这是一个LDT描述符。
         mov eax,[es:esi+0x0c]              ;LDT的起始线性地址  从TCB取出
         movzx ebx,word [es:esi+0x0a]       ;LDT段界限   从TCB取出
         mov ecx,0x00408200                 ;LDT描述符，特权级0  D位（或者叫B 位）和L位对LDT描述符来说没有意义，固定为0。AVL和P位的含义和存储器的段描述符相同。S位固定为0，表示系统的段描述符或者门描述符，以相对于存储器的段描述符（S＝1），因为LDT描述符属于系统的段描述符。
         call sys_routine_seg_sel:make_seg_descriptor   ;309行，构造LDT描述符
         call sys_routine_seg_sel:set_up_gdt_descriptor ;264行，将LDT描述符写入GDT
         mov [es:esi+0x10],cx               ;登记LDT选择子到TCB中 0x10-0x11存LDT段选择子
;LDT描述符格式：低32位：0-15位为段界限0-15,16-31为段基地址0-15 高32位：0-7为段基地址16-23,8-11为TYPE，12为S，13-14为DPL，15为P，16-19为段界限16-19，20AVL，21L，22D，23G，24-31为段基地址24-31
         ;创建用户程序的TSS  和LDT一样，必须在全局描述符表（GDT）中创建每个TSS的描述符。
         mov ecx,104                        ;tss的基本尺寸
         mov [es:esi+0x12],cx      ;cx存的tss尺寸，TCB的0x12-0x13用来保存tss界限值
         dec word [es:esi+0x12]             ;登记TSS界限值到TCB 
         call sys_routine_seg_sel:allocate_memory       ;分配内存，大小位ecx的值，返回起始地址，保存在ecx中
         mov [es:esi+0x14],ecx              ;登记TSS基地址到TCB TCB的0x14-0x17用来保存TSS的基地址
      
         ;登记基本的TSS表格内容    将指向前一个任务的指针（任务链接域）填写为0，表明这是唯一的任务。
         mov word [es:ecx+0],0              ;反向链=0
       ;693～709行，登记0、1和2特权级栈的段选择子，以及它们的初始栈指针。所有的栈信息都在TCB中，先从TCB中取出，然后填写到TSS中的相应位置。
         mov edx,[es:esi+0x24]              ;登记0特权级堆栈初始ESP   从TCB中取出0特权级的堆栈初始ESP
         mov [es:ecx+4],edx                 ;到TSS中
       
         mov dx,[es:esi+0x22]               ;登记0特权级堆栈段选择子  从TCB中取出0特权级堆栈段选择子
         mov [es:ecx+8],dx                  ;到TSS中
      
         mov edx,[es:esi+0x32]              ;登记1特权级堆栈初始ESP   从TCB中取出1特权级的堆栈初始ESP
         mov [es:ecx+12],edx                ;到TSS中

         mov dx,[es:esi+0x30]               ;登记1特权级堆栈段选择子  从TCB中取出1特权级堆栈段选择子
         mov [es:ecx+16],dx                 ;到TSS中

         mov edx,[es:esi+0x40]              ;登记2特权级堆栈初始ESP   从TCB中取出2特权级的堆栈初始ESP
         mov [es:ecx+20],edx                ;到TSS中

         mov dx,[es:esi+0x3e]               ;登记2特权级堆栈段选择子  从TCB中取出2特权级堆栈段选择子
         mov [es:ecx+24],dx                 ;到TSS中

         mov dx,[es:esi+0x10]               ;登记任务的LDT选择子  从TCB中取出LDT选择子
         mov [es:ecx+96],dx                 ;到TSS中
;714、715 行，填写I/O许可位映射区的地址。在这里，填写的是TSS段界限（103），这意味着不存在该区域。
         mov dx,[es:esi+0x12]               ;登记任务的I/O位图偏移  从TCB中取出TSS界限值
         mov [es:ecx+102],dx                ;到TSS中  
;在TSS内偏移为102的那个字单元，保存着I/O许可位串（I/O许可位映射区）的起始位置。如果该字单元的内容大于或者等于TSS的段界限（在TSS 描述符中），则表明没有I/O许可位串    
         mov word [es:ecx+100],0            ;T=0
       
         ;在GDT中登记TSS描述符 一方面是为了对TSS进行段和特权级的检查；另一方面，也是执行任务切换的需要。
         mov eax,[es:esi+0x14]              ;TSS的起始线性地址   从TCB中获得
         movzx ebx,word [es:esi+0x12]       ;段长度（界限）      从TCB中获得
         mov ecx,0x00408900                 ;TSS描述符，特权级0
         call sys_routine_seg_sel:make_seg_descriptor   ;创建TSS描述符
         call sys_routine_seg_sel:set_up_gdt_descriptor ;安装到GDT上，cx返回段选择子，RPL为0
         mov [es:esi+0x18],cx               ;登记TSS选择子到TCB
;TSS描述符格式：低32位：0-15位为段界限0-15,16-31为段基地址0-15 高32位：0-7为段基地址16-23,8-11为TYPE，12为S，13-14为DPL，15为P，16-19为段界限16-19，20AVL，21L，22D，23G，24-31为段基地址24-31
         pop es                             ;恢复到调用此过程前的es段 
         pop ds                             ;恢复到调用此过程前的ds段
      
         popad
       ;指令类型：ret imm16   retf imm16 它指示在将控制返回到调用者之前，应当从栈中弹出多少字节的数据。
         ret 8                              ;丢弃调用本过程前压入的参数 
       ;除了将控制返回到过程的调用者之外，还要调整栈的指针ESP <- ESP+8
;-------------------------------------------------------------------------------
append_to_tcb_link:                         ;在TCB链上追加任务控制块
                                            ;输入：ECX=TCB线性基地址
         push eax
         push edx
         push ds
         push es
       ;为了访问链首指针tcb_chain，让DS指向内核数据段
         mov eax,core_data_seg_sel          ;令DS指向内核数据段 
         mov ds,eax
         mov eax,mem_0_4_gb_seg_sel         ;令ES指向0..4GB段
         mov es,eax  ;令es指向0~4G段，因为链上的每个TCB，其空间都是动态分配的(allocate_memory)，只能通过线性地址来访问
         
         mov dword [es: ecx+0x00],0         ;当前TCB指针域清零，以指示这是最
                                            ;后一个TCB
       ;750~752判断TCB头指针是否为空
         mov eax,[tcb_chain]                ;TCB表头指针  是一个指针，用来指向第一个任务的TCB线性基地址 
         or eax,eax                         ;链表为空？
         jz .notcb   ;TCB（任务控制块）链表为空，就直接添加就可以
       ;不为空，循环到最后一个TCB节点，让最后一个节点的指针，指向当前TCb
  .searc:
         mov edx,eax
         mov eax,[es: edx+0x00]    ;将下一个TCB地址给eax，每个TCB节点需要通过4GB的段来访问，因为其没在内核数据段分配，而是调用allocate_memory分配的
         or eax,eax  ;eax存的是下一个TCB的地址,如果为0表示到了最后一个TCB节点
         jnz .searc
         
         mov [es: edx+0x00],ecx    ;将要添加的TCB节点挂到TCB链表上
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
start:
         mov ecx,core_data_seg_sel          ;使ds指向核心数据段 
         mov ds,ecx
       ;显示message_1
         mov ebx,message_1         ;message_1在388行                 
         call sys_routine_seg_sel:put_string
                                         
         ;显示处理器品牌信息 
         mov eax,0x80000002 ;eax存储你要调用的服务号
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
       ;812～826 行用于安装调用门的描述符
         ;以下开始安装为整个系统服务的调用门。特权级之间的控制转移必须使用门
         mov edi,salt                       ;C-SALT表的起始位置 
         mov ecx,salt_items                 ;C-SALT表的条目数量 
  .b3:
         push ecx   
         mov eax,[edi+256]                  ;该条目入口点的32位偏移地址 
         mov bx,[edi+260]                   ;该条目入口点的段选择子 
         mov cx,1_11_0_1100_000_00000B      ;特权级3的调用门(3以上的特权级才
;CX存调用门属性P，DPL，0，TYPE,000，参数个数0~4位;允许访问)，0个参数(因为用寄存器
;3个参数，eax，bx和cx，TYPE1100表示调用门       ;传递参数，而没有用栈) 
         call sys_routine_seg_sel:make_gate_descriptor  ;331行，构造调用门描述符
         call sys_routine_seg_sel:set_up_gdt_descriptor ;创建调用门描述符 返回调用门描述符选择子，RPL为0 264行
         mov [edi+260],cx                   ;将返回的门描述符选择子回填  覆盖原先的代码段选择子
         add edi,salt_item_len              ;指向下一个C-SALT条目 
         pop ecx
         loop .b3

         ;对门进行测试      使用调用门显示message_2的信息
         mov ebx,message_2  ;message_2在393行
         call far [salt_1+256] ;365行        ;通过门显示信息(偏移量将被忽略) salt_1条目给出的32位偏移量部分被丢弃 
       ;显示message_3的信息，message_3在395行
         mov ebx,message_3                    
         call sys_routine_seg_sel:put_string ;在内核中调用例程不需要通过门
       ;836~838分配创建TCB所需要的内存空间，并将其挂在TCB链上。
         ;创建任务控制块。这不是处理器的要求，而是我们自己为了方便而设立的
         mov ecx,0x46       ;TCB（任务控制块）大小为0x46，保存一些任务的信息和状态。如0、1、2特权级的栈地址，大小，TSS选择子，基地址，界限，LDT选择子，基地址，界限，下一个TCB的地址等...
         call sys_routine_seg_sel:allocate_memory ;ECX返回分配的内存的起始线性地址 
         call append_to_tcb_link   ;735行    ;将任务控制块追加到TCB链表
       ;通过栈传递参数，ecx为调用allocate_memory返回值，保存的TCB的起始线性地址
         push dword 50    ;0x32             ;用户程序位于逻辑50扇区
         push ecx                           ;压入任务控制块起始线性地址 
       
         call load_relocate_program       ;464行
       ;显示一条成功的消息。
         mov ebx,do_status
         call sys_routine_seg_sel:put_string
      
         mov eax,mem_0_4_gb_seg_sel
         mov ds,eax
       ;加载任务寄存器TR和局部描述符表寄存器（LDTR）。  ltr r/m16     lldt r/m16
         ltr [ecx+0x18]                     ;加载任务状态选择子到TR  从TCB取得起始地址
         lldt [ecx+0x10]                    ;加载LDT选择子到ldtr    从TCB取得起始地址
       ;TR和LDTR格式：16位段选择子+高速缓存（32位线性基地址+段界限+段属性）
         mov eax,[ecx+0x44] ;访问任务的TCB，从中取出用户程序头部段选择子，并传送到段寄存器DS，该段选择子RPL=3，TI=1
         mov ds,eax                         ;切换到用户程序头部段 
;858～862 行，从用户程序头部内取出栈段选择子和栈指针，以及代码段选择子和入口点，并将它们顺序压入当前的0特权级栈中
         ;以下假装是从调用门返回。摹仿处理器压入返回参数 
         push dword [0x08]                  ;调用前的堆栈段选择子
         push dword 0                       ;调用前的esp

         push dword [0x14]                  ;调用前的代码段选择子 
         push dword [0x10]                  ;调用前的eip
      
         retf        ;假装从调用门返回
;注意，这里所用的0特权级栈并非是来自于TSS。不过，处理器不会在意这个。下次，从3特权级的段再次来到0特权级执行时，就会用到TSS中的0特权级栈了。
return_point:                               ;用户程序返回点
         mov eax,core_data_seg_sel          ;因为c14.asm是以JMP的方式使用调 
         mov ds,eax                         ;用门@TerminateProgram，回到这 
                                            ;里时，特权级为3，会导致异常。 
         mov ebx,message_6
         call sys_routine_seg_sel:put_string

         hlt
            
core_code_end:

;-------------------------------------------------------------------------------
SECTION core_trail
;-------------------------------------------------------------------------------
core_end: