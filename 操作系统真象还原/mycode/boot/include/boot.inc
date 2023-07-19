;-------------	 loader和kernel   ----------
; 设置loader的基地址和起始扇区号
LOADER_BASE_ADDR equ 0x900 
LOADER_START_SECTOR equ 0x2
KERNEL_BIN_BASE_ADDR equ 0x70000
KERNEL_IMAGE_BASE_ADDR  equ   0x1500
KERNEL_START_SECTOR equ 0x9
KERNEL_ENTRY_POINT equ 0xc0001500

PAGE_DIR_TABLE_POS equ 0x100000 ;二级页目录表，页表放在内存中1M起始位置连续存放，尽可能简单，超出低端1MB的第一个字节，0x100000=2的20次方

;--------------   gdt描述符属性  -------------
; 页粒度标志，表示段的粒度是4KB，这里设置为1
DESC_G_4K   equ	  1_00000000000000000000000b  ;表示描述符的G位，为4K粒度
; 默认操作大小标志，表示代码段默认使用32位操作数，这里设置为1
DESC_D_32   equ	   1_0000000000000000000000b  ;表示32位操作数
; 长模式标记，表示支持64位代码，这里设置为0
DESC_L	    equ	    0_000000000000000000000b	;  64位代码标记，此处标记为0便可。为0，表示为32位代码段。
; CPU不使用的位，设置为0
DESC_AVL    equ	     0_00000000000000000000b	;  cpu不用此位，留给操作系统使用的，暂置为0  
; 代码段界限的16-19位
DESC_LIMIT_CODE2  equ 1111_0000000000000000b  ;代码段段界限的第二部分（高4字节中的一部分）
; 数据段界限的16-19位
DESC_LIMIT_DATA2  equ DESC_LIMIT_CODE2
; 显存段界限的16-19位，段基址的16-23位
DESC_LIMIT_VIDEO2  equ 0000_000000000000000b
; 存在位，表示段是否存在，这里设置为1
DESC_P	    equ		  1_000000000000000b  ;表示段存在
; 特权级，表示代码段和数据段的特权级为0，这里设置为0
DESC_DPL_0  equ		   00_0000000000000b  ;该段描述符对应的内存段的特权级是0
DESC_DPL_1  equ		   01_0000000000000b
DESC_DPL_2  equ		   10_0000000000000b
DESC_DPL_3  equ		   11_0000000000000b
DESC_S_CODE equ		     1_000000000000b  ;此值为1，表示它是个普通的内存段，不是系统段。
DESC_S_DATA equ	  DESC_S_CODE
DESC_S_sys  equ		     0_000000000000b  ;此值为0，表示这是一个系统段，比如门描述符，gdt，tss，ldt描述符
DESC_TYPE_CODE  equ	      1000_00000000b	;x=1,c=0,r=0,a=0 代码段是可执行的,非依从的,不可读的,已访问位a清0.  
DESC_TYPE_DATA  equ	      0010_00000000b	;x=0,e=0,w=1,a=0 数据段是不可执行的,向上扩展的,可写的,已访问位a清0.
;凑齐代码段段描述符高4位，采用平坦模式，所以段基址定义为0x0000，开始的0x00<<24和最后的0x00表示段基址的24-31和16-23位
DESC_CODE_HIGH4 equ (0x00 << 24) + DESC_G_4K + DESC_D_32 + DESC_L + DESC_AVL + DESC_LIMIT_CODE2 + DESC_P + DESC_DPL_0 + DESC_S_CODE + DESC_TYPE_CODE + 0x00
DESC_DATA_HIGH4 equ (0x00 << 24) + DESC_G_4K + DESC_D_32 + DESC_L + DESC_AVL + DESC_LIMIT_DATA2 + DESC_P + DESC_DPL_0 + DESC_S_DATA + DESC_TYPE_DATA + 0x00
DESC_VIDEO_HIGH4 equ (0x00 << 24) + DESC_G_4K + DESC_D_32 + DESC_L + DESC_AVL + DESC_LIMIT_VIDEO2 + DESC_P + DESC_DPL_0 + DESC_S_DATA + DESC_TYPE_DATA + 0x0b

;--------------   选择子属性  ---------------
RPL0  equ   00b
RPL1  equ   01b
RPL2  equ   10b
RPL3  equ   11b
TI_GDT	 equ   000b
TI_LDT	 equ   100b

;----------------   页表相关属性    --------------
PG_P  equ   1b      ;表示页面存在位，即页面是否在物理内存中存在，PG_P = 1时，表示页面存在于物理内存中
PG_RW_R	 equ  00b   ;表示页面只允许读取操作，不允许写入操作
PG_RW_W	 equ  10b   ;表示页面既允许读取操作，又允许写入操作
PG_US_S	 equ  000b  ;表示页面只能由特权级程序进行访问(0、1、2)
PG_US_U	 equ  100b  ;表示用户特权级访问权限位，即页面是否允许用户特权级的程序对其进行访问。

;-------------  program type 定义   --------------
PT_NULL equ 0