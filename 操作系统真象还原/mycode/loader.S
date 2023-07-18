   %include "boot.inc"
   section loader vstart=LOADER_BASE_ADDR
   LOADER_STACK_TOP equ LOADER_BASE_ADDR  ;用于loader在保护模式下的栈

;构建gdt及其内部的描述符   0号段描述符，不可用
   GDT_BASE:   dd    0x00000000 
	       dd    0x00000000
;代码段描述符
   CODE_DESC:  dd    0x0000FFFF  ;低4字节，0-15位（段界限0-15位），16-31位（段基址0-15位）
	       dd    DESC_CODE_HIGH4
;数据段和栈段的段描述符，数据段和栈段共同使用一个段描述符   段描述符的向上或向下拓展属性，是CPU在检查段界限时使用的，向上拓展的段同样能做栈段，只是访问内存时，段界限的检查按照向上拓展的段的方式检查。
   DATA_STACK_DESC:  dd    0x0000FFFF
		     dd    DESC_DATA_HIGH4
;显存段描述符  未采用平坦模型，其他段都采用平坦模型   段基址0xb8000
   VIDEO_DESC: dd    0x80000007	       ;limit=(0xbffff-0xb8000)/4k=0x7
	       dd    DESC_VIDEO_HIGH4  ; 此时dpl已改为0

   GDT_SIZE   equ   $ - GDT_BASE ;GDT大小
   GDT_LIMIT   equ   GDT_SIZE -	1  ;GDT界限
   times 60 dq 0	; 此处预留60个描述符的slot  往GDT中添加其他描述符，提前保留空间而已。
   SELECTOR_CODE equ (0x0001<<3) + TI_GDT + RPL0    ; 相当于(CODE_DESC - GDT_BASE)/8 + TI_GDT + RPL0
   SELECTOR_DATA equ (0x0002<<3) + TI_GDT + RPL0	 ; 同上  栈段段选择子也用这个选择子（数据段段选择子）
   SELECTOR_VIDEO equ (0x0003<<3) + TI_GDT + RPL0	 ; 同上 
   ; (4+60)*8=512=0x200 字节
   ; total_mem_bytes用于保存内存容量,以字节为单位,此位置比较好记。
   ; 当前偏移loader.bin文件头0x200字节,loader.bin的加载地址是0x900,
   ; 故total_mem_bytes内存中的地址是0xb00.将来在内核中咱们会引用此地址
   total_mem_bytes dd 0
  
   ;以下是定义全局描述符表gdt的指针，前2字节是gdt界限，后4字节是gdt起始地址
   gdt_ptr  dw  GDT_LIMIT 
	    dd  GDT_BASE
   ;35～36行是提前定义的缓冲区，为的是存储BIOS 0x15中断0xe820子功能返回的ARDS结构。
   ;人工对齐:total_mem_bytes4字节+gdt_ptr6字节+ards_buf244字节+ards_nr2,共256字节（0x100）
   ards_buf times 244 db 0 ;存储 BIOS0x15中断0xe820子功能返回的ARDS结构。
   ards_nr dw 0            ;用于记录ards结构体数量
;上面分配244字节，这是为了手工对齐下面39行的标签loader_start，使其在文件内的偏移地址为0x300。

loader_start:
;-------  int 15h eax = 0000E820h ,edx = 534D4150h ('SMAP') 获取内存布局  -------
;es:di存放缓冲区地址，es在mbr中已经赋值了(0)，45行队di赋值。
   xor ebx, ebx		      ;第一次调用时，ebx值要为0
   mov edx, 0x534d4150	      ;edx只赋值一次，循环体中不会改变 固定签名，16进制数字字符串是SMAP的ASCII
   mov di, ards_buf	      ;ards结构缓冲区
.e820_mem_get_loop:	      ;循环获取每个ARDS内存范围描述结构
   mov eax, 0x0000e820	      ;执行int 0x15后,eax值变为0x534d4150,所以每次执行int前都要更新为子功能号。
   mov ecx, 20		      ;ARDS地址范围描述符结构大小是20字节 每次执行完ecx会改变，所以每次都需要给ecx重新赋值
   int 0x15
   jc .e820_failed_so_try_e801   ;若cf位为1则有错误发生，尝试0xe801子功能
   add di, cx		      ;使di增加20字节指向缓冲区中新的ARDS结构位置
   inc word [ards_nr]	      ;记录ARDS数量  每次得到一个ARDS后，便将ards_nr加1,记录ARDS的个数
   cmp ebx, 0		      ;若ebx为0且cf不为1,这说明ards全部返回，当前已是最后一个
   jnz .e820_mem_get_loop
;55～68行是找出最大的内存块。思路是对每一个ARDS结构中的BaseAddrLow与LengthLow相加求和，遍历完所有ARDS，值最大的则为内存容量，由于BaseAddrLow+LengthLow的单位是字节而无需转换，之后便直接跳转到.mem_get_ok，将此容量数写入变量total_mem_bytes
;在所有ards结构中，找出(base_add_low + length_low)的最大值，即内存的容量。
   mov cx, [ards_nr]	      ;遍历每一个ARDS结构体,循环次数是ARDS的数量
   mov ebx, ards_buf 
   xor edx, edx		      ;edx为最大的内存容量,在此先清0
.find_max_mem_area:	      ;无须判断type是否为1,最大的内存块一定是可被使用
   mov eax, [ebx]	      ;base_add_low
   add eax, [ebx+8]	      ;length_low
   add ebx, 20		      ;指向缓冲区中下一个ARDS结构
   cmp edx, eax		      ;冒泡排序，找出最大,edx寄存器始终是最大的内存容量
   jge .next_ards
   mov edx, eax		      ;edx为总内存大小
.next_ards:
   loop .find_max_mem_area
   jmp .mem_get_ok
;72～94行是利用子功能0xe801探测内存容量
;------  int 15h ax = E801h 获取内存大小,最大支持4G  ------
; 返回后, ax cx 值一样,以KB为单位,bx dx值一样,以64KB为单位
; 在ax和cx寄存器中为低16M,在bx和dx寄存器中为16MB到4G。
.e820_failed_so_try_e801:
   mov ax,0xe801
   int 0x15    ;cf为0代表成功，为1代表失败，ax和cx以1KB为单位，只显示低15MB内存，bx和dx以64KB为单位，显示16MB-4GB中连续的内存单位数量
   jc .e801_failed_so_try88   ;若当前e801方法失败,就尝试0x88方法

;1 先算出低15M的内存,ax和cx中是以KB为单位的内存数量,将其转换为以byte为单位
   mov cx,0x400	     ;cx和ax值一样,cx用做乘数  cx=1024
   mul cx      ;积的高16位在DX寄存器，低16位在AX寄存器
   shl edx,16  ;将EDX左移16位后再与AX做或运算便得到了完整32位的积。
   and eax,0x0000FFFF
   or edx,eax
   add edx, 0x100000 ;ax只是15MB,故要加1MB 获取到的内存总比实际大小少1MB，故在此“补偿”。
   mov esi,edx	     ;先把低15MB的内存容量存入esi寄存器备份
;87～94行是计算16MB之上的内存容量，结果存放在寄存器BX和DX，单位是64KB，所以也要将其转换为字节。
;2 再将16MB以上的内存转换为byte为单位,寄存器bx和dx中是以64KB为单位的内存数量
   xor eax,eax
   mov ax,bx		
   mov ecx, 0x10000	;0x10000十进制为64KB
   mul ecx		;32位乘法,默认的被乘数是eax,积为64位,高32位存入edx,低32位存入eax.
   add esi,eax		;由于此方法只能测出4G以内的内存,故32位eax足够了,edx肯定为0,只加eax便可
   mov edx,esi		;edx为总内存大小
   jmp .mem_get_ok
;97～109行是用子功能0x88方法探测内存容量
;-----------------  int 15h ah = 0x88 获取内存大小,只能获取64M之内  ----------
.e801_failed_so_try88: 
   ;int 15后，ax存入的是以kb为单位的内存容量
   mov  ah, 0x88
   int  0x15   ;cf为0表示成功，cf为1表示出错。ax以1KB为单位大小，内存空间1MB只上的连续单位数量，不包括低端1MB内存。
   jc .error_hlt
   and eax,0x0000FFFF
      
   ;16位乘法，被乘数是ax,积为32位.积的高16位在dx中，积的低16位在ax中
   mov cx, 0x400     ;0x400等于1024,将ax中的内存容量换为以byte为单位
   mul cx   ;积的高16位在DX寄存器，低16位在AX寄存器
   shl edx, 16	     ;把dx移到高16位
   or edx, eax	     ;把积的低16位组合到edx,为32位的积
   add edx,0x100000  ;0x88子功能只会返回1MB以上的内存,故实际内存大小要加上1MB

.mem_get_ok:
   mov [total_mem_bytes], edx	 ;将内存换为byte单位后存入total_mem_bytes处。

;----------------------------------------   准备进入保护模式   ------------------------------------------
									;1 打开A20
									;2 加载gdt
									;3 将cr0的pe位置1


   ;-----------------  打开A20  ----------------
   in al,0x92
   or al,0000_0010B
   out 0x92,al

   ;-----------------  加载GDT  ----------------
   lgdt [gdt_ptr]


   ;-----------------  cr0第0位置1  ----------------
   mov eax, cr0
   or eax, 0x00000001      ;将PE位置为1
   mov cr0, eax
;自105行之后，CPU便进入了保护模式，故第107行的指令已经是在保护模式下了，但它依然还是16位的指令，
;相当于处于16位保护模式下。为了让其使用32位偏移地址，所以添加了伪指令dword，故其机器码前会加0x66反转前缀。
;描述符缓冲寄存器在实模式下时已经在使用了，其低20位是段基址，但其他位默认为0，也就是描述符中的D位为0，
;这表示当前的操作数大小是16位。因为当前段描述符缓冲寄存器中的D位是0，所以操作数是16位，
;当前属于16位保护模式。故，在这里也可以把 dword 去掉，毕竟当前操作数大小就是16位。而且p_mode_start的地址并没有超过16位
   ;jmp dword SELECTOR_CODE:p_mode_start	     ; 刷新流水线，避免分支预测的影响,这种cpu优化策略，最怕jmp跳转，  66ea4b0b00000800  jmp far 0008：00000b4b
   jmp  SELECTOR_CODE:p_mode_start	     ; 刷新流水线，避免分支预测的影响,这种cpu优化策略，最怕jmp跳转，    ea480b0800    jmp far 0008：0b48
					     ; 这将导致之前做的预测失效，从而起到了刷新的作用。
.error_hlt:    ;出错则挂起
   hlt

[bits 32]
p_mode_start:
   mov ax, SELECTOR_DATA
   mov ds, ax
   mov es, ax
   mov ss, ax
   mov esp,LOADER_STACK_TOP
   mov ax, SELECTOR_VIDEO
   mov gs, ax
   ; 创建页目录及页表并初始化页内存位图
   call setup_page
   ;第152～153行，是为了重启加载GDT做准备。因为我们在页表中会将内核放置到3GB以上的地址，我们也把GDT放在内核的地址空间
   ;要将描述符表地址及偏移量写入内存gdt_ptr,一会用新地址重新加载
   sgdt [gdt_ptr]          ; 存储到原来gdt所有的位置

   ;将gdt描述符中视频段描述符中的段基址+0xc0000000
   mov ebx, [gdt_ptr + 2]        ;获取gdt基地址
   or dword [ebx + 0x18 + 4], 0xc0000000  ;视频段是第3个段描述符,每个描述符是8字节,故0x18。由于描述符的高位都是0,所以直接or就可以
               ;段描述符的高4字节的最高位是段基址的31~24位，所以ebx不仅要加上0x18，还要加上0x4
   ;将gdt的基址加上0xc0000000使其成为内核所在的高地址
   add dword [gdt_ptr + 2], 0xc0000000
   
   add esp, 0xc0000000

   ; 把页目录地址赋给cr3
   mov eax, PAGE_DIR_TABLE_POS
   mov cr3, eax

   ; 打开cr0的pg位(第31位)
   mov eax, cr0
   or eax, 0x80000000
   mov cr0, eax

   ;在开启分页后,用gdt新的地址重新加载
   lgdt [gdt_ptr]             ; 重新加载
   ;160是指第二行开始的位置，一个字符是 2字节，每行是80个字符
   mov byte [gs:160], 'V'     ;视频段段基址已经被更新,用字符v表示virtual addr
   mov byte [gs:162], 'i'     ;视频段段基址已经被更新,用字符v表示virtual addr
   mov byte [gs:164], 'r'     ;视频段段基址已经被更新,用字符v表示virtual addr
   mov byte [gs:166], 't'     ;视频段段基址已经被更新,用字符v表示virtual addr
   mov byte [gs:168], 'u'     ;视频段段基址已经被更新,用字符v表示virtual addr
   mov byte [gs:170], 'a'     ;视频段段基址已经被更新,用字符v表示virtual addr
   mov byte [gs:172], 'l'     ;视频段段基址已经被更新,用字符v表示virtual addr

   jmp $

;-------------   创建页目录及页表   ---------------
setup_page:
;先把页目录占用的空间逐字节清0
   mov ecx, 4096
   mov esi, 0
.clear_page_dir:  ;通过循环清空页目录表所占的内存
   mov byte [PAGE_DIR_TABLE_POS + esi], 0    ;PAGE_DIR_TABLE_POS是个宏，定义在include.inc中，表示页目录表的基地址
   inc esi
   loop .clear_page_dir

;开始创建页目录项(PDE)
.create_pde:				     ; 创建Page Directory Entry
   mov eax, PAGE_DIR_TABLE_POS
   add eax, 0x1000 			     ; 此时eax为第一个页表的位置及属性
   mov ebx, eax				     ; 此处为ebx赋值，是为.create_pte做准备，ebx为基址。

;   下面将页目录项0和0xc00都存为第一个页表的地址，分别将其存入到页目录项的第0项和第768项，0xc00/4=768
;   一个页表可表示4MB内存,这样0xc03fffff以下的地址和0x003fffff以下的地址都指向相同的页表，
;   这是为将地址映射为内核地址做准备  
   or eax, PG_US_U | PG_RW_W | PG_P	     ; 页目录项的属性RW和P位为1,US为1,表示用户属性,所有特权级别都可以访问.  PG_US_U | PG_RW_W | PG_P=0x7
   mov [PAGE_DIR_TABLE_POS + 0x0], eax       ; 第1个目录项,在页目录表中的第1个目录项写入第一个页表的位置(0x101000)及属性(7)
   mov [PAGE_DIR_TABLE_POS + 0xc00], eax     ; 一个页表项占用4字节,0xc00表示第768个页表占用的目录项,0xc00以上的目录项用于内核空间,
					     ; 也就是页表的0xc0000000~0xffffffff共计1G属于内核,0x0~0xbfffffff共计3G属于用户进程.
   sub eax, 0x1000   ;在页目录的最后一个页目录项中写入页表自己的物理地址  eax = 0x101007-0x1000=0x100007
   mov [PAGE_DIR_TABLE_POS + 4092], eax	     ; 使最后一个目录项指向页目录表自己的地址

;下面创建页表项(PTE)
   mov ecx, 256				     ; 1M低端内存 / 每页大小4k = 256
   mov esi, 0
   mov edx, PG_US_U | PG_RW_W | PG_P	     ; 属性为7,US=1,RW=1,P=1
.create_pte:				     ; 创建Page Table Entry
   mov [ebx+esi*4],edx			     ; 此时的ebx已经在上面通过eax赋值为0x101000,也就是第一个页表的地址 
   add edx,4096      ; edx是物理页的页表项，每次edx加上4KB，其物理地址是连续分配的，即在低端1MB内存中，虚拟地址等于物理地址。
   inc esi
   loop .create_pte

;创建内核其它页表的PDE  创建除第768个页表之外的其他页表对应的PDE，也就是内核空间中除第0个页表外的其余所有页表对应的目录项。
   mov eax, PAGE_DIR_TABLE_POS
   add eax, 0x2000 		     ; 此时eax为第二个页表的位置
   or eax, PG_US_U | PG_RW_W | PG_P  ; 页目录项的属性US,RW和P位都为1
   mov ebx, PAGE_DIR_TABLE_POS
   mov ecx, 254			     ; 内核对应3GB-4GB，所以范围为第769~1022的所有目录项数量  在页目录表中为内核额外安装254个页表的PDE（第255个PDE已经指向了页目录表本身，第768个已经安装）
   mov esi, 769
.create_kernel_pde:
   mov [ebx+esi*4], eax
   inc esi
   add eax, 0x1000
   loop .create_kernel_pde
   ret