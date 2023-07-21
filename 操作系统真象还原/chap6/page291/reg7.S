	.file	"reg7.c"
	.text
.Ltext0:
	.file 0 "/home/cccmmf/\346\223\215\344\275\234\347\263\273\347\273\237/\346\223\215\344\275\234\347\263\273\347\273\237\347\234\237\350\261\241\350\277\230\345\216\237/chap6/page291" "reg7.c"
	.section	.rodata
.LC0:
	.string	"hello,world\n"
	.align 4
.LC1:
	.string	"the number of bytes written is %d\n"
	.text
	.globl	main
	.type	main, @function
main:
.LFB0:
	.file 1 "reg7.c"
	.loc 1 4 13
	.cfi_startproc
	leal	4(%esp), %ecx
	.cfi_def_cfa 1, 0
	andl	$-16, %esp
	pushl	-4(%ecx)
	pushl	%ebp
	movl	%esp, %ebp
	.cfi_escape 0x10,0x5,0x2,0x75,0
	pushl	%ebx
	pushl	%ecx
	.cfi_escape 0xf,0x3,0x75,0x78,0x6
	.cfi_escape 0x10,0x3,0x2,0x75,0x7c
	subl	$16, %esp
	call	__x86.get_pc_thunk.dx
	addl	$_GLOBAL_OFFSET_TABLE_, %edx
	.loc 1 4 13
	movl	%gs:20, %eax
	movl	%eax, -12(%ebp)
	xorl	%eax, %eax
	.loc 1 5 7
	movl	$0, -20(%ebp)		;变量 ret_cnt
	.loc 1 5 20
	movl	$0, -16(%ebp)		;变量test
	.loc 1 6 9
	leal	.LC0@GOTOFF(%edx), %eax
	movl	%eax, -24(%ebp)
	.loc 1 14 3
	movl	-16(%ebp), %eax
#APP
# 14 "reg7.c" 1
	;把立即数6movl到了寄存器eax，这破坏了printf的返回值。
	pushl -24(%ebp);         call printf;            addl $4, %esp;         movl $6, %eax		
# 0 "" 2
#NO_APP
	movl	%eax, -20(%ebp)
	.loc 1 21 3
	subl	$8, %esp
	pushl	-20(%ebp)
	leal	.LC1@GOTOFF(%edx), %eax
	pushl	%eax
	movl	%edx, %ebx
	call	printf@PLT
	addl	$16, %esp
	.loc 1 22 1
	nop
	movl	-12(%ebp), %eax
	subl	%gs:20, %eax
	je	.L2
	call	__stack_chk_fail_local
.L2:
	leal	-8(%ebp), %esp
	popl	%ecx
	.cfi_restore 1
	.cfi_def_cfa 1, 0
	popl	%ebx
	.cfi_restore 3
	popl	%ebp
	.cfi_restore 5
	leal	-4(%ecx), %esp
	.cfi_def_cfa 4, 4
	ret
	.cfi_endproc
.LFE0:
	.size	main, .-main
	.section	.text.__x86.get_pc_thunk.dx,"axG",@progbits,__x86.get_pc_thunk.dx,comdat
	.globl	__x86.get_pc_thunk.dx
	.hidden	__x86.get_pc_thunk.dx
	.type	__x86.get_pc_thunk.dx, @function
__x86.get_pc_thunk.dx:
.LFB1:
	.cfi_startproc
	movl	(%esp), %edx
	ret
	.cfi_endproc
.LFE1:
	.text
.Letext0:
	.file 2 "/usr/include/stdio.h"
	.section	.debug_info,"",@progbits
.Ldebug_info0:
	.long	0xd3
	.value	0x5
	.byte	0x1
	.byte	0x4
	.long	.Ldebug_abbrev0
	.uleb128 0x4
	.long	.LASF16
	.byte	0x1d
	.long	.LASF0
	.long	.LASF1
	.long	.Ltext0
	.long	.Letext0-.Ltext0
	.long	.Ldebug_line0
	.uleb128 0x1
	.byte	0x4
	.byte	0x7
	.long	.LASF2
	.uleb128 0x1
	.byte	0x1
	.byte	0x6
	.long	.LASF3
	.uleb128 0x5
	.long	0x2d
	.uleb128 0x1
	.byte	0x1
	.byte	0x8
	.long	.LASF4
	.uleb128 0x1
	.byte	0x2
	.byte	0x7
	.long	.LASF5
	.uleb128 0x1
	.byte	0x4
	.byte	0x7
	.long	.LASF6
	.uleb128 0x1
	.byte	0x1
	.byte	0x6
	.long	.LASF7
	.uleb128 0x1
	.byte	0x2
	.byte	0x5
	.long	.LASF8
	.uleb128 0x6
	.byte	0x4
	.byte	0x5
	.string	"int"
	.uleb128 0x1
	.byte	0x8
	.byte	0x5
	.long	.LASF9
	.uleb128 0x1
	.byte	0x8
	.byte	0x7
	.long	.LASF10
	.uleb128 0x1
	.byte	0x4
	.byte	0x5
	.long	.LASF11
	.uleb128 0x2
	.long	0x2d
	.uleb128 0x7
	.long	.LASF12
	.byte	0x2
	.value	0x164
	.byte	0xc
	.long	0x5c
	.long	0x95
	.uleb128 0x8
	.long	0x95
	.uleb128 0x9
	.byte	0
	.uleb128 0x2
	.long	0x34
	.uleb128 0xa
	.long	.LASF13
	.byte	0x1
	.byte	0x4
	.byte	0x6
	.long	.LFB0
	.long	.LFE0-.LFB0
	.uleb128 0x1
	.byte	0x9c
	.uleb128 0x3
	.long	.LASF14
	.byte	0x7
	.long	0x5c
	.uleb128 0x2
	.byte	0x75
	.sleb128 -20
	.uleb128 0x3
	.long	.LASF15
	.byte	0x14
	.long	0x5c
	.uleb128 0x2
	.byte	0x75
	.sleb128 -16
	.uleb128 0xb
	.string	"fmt"
	.byte	0x1
	.byte	0x6
	.byte	0x9
	.long	0x78
	.uleb128 0x2
	.byte	0x75
	.sleb128 -24
	.byte	0
	.byte	0
	.section	.debug_abbrev,"",@progbits
.Ldebug_abbrev0:
	.uleb128 0x1
	.uleb128 0x24
	.byte	0
	.uleb128 0xb
	.uleb128 0xb
	.uleb128 0x3e
	.uleb128 0xb
	.uleb128 0x3
	.uleb128 0xe
	.byte	0
	.byte	0
	.uleb128 0x2
	.uleb128 0xf
	.byte	0
	.uleb128 0xb
	.uleb128 0x21
	.sleb128 4
	.uleb128 0x49
	.uleb128 0x13
	.byte	0
	.byte	0
	.uleb128 0x3
	.uleb128 0x34
	.byte	0
	.uleb128 0x3
	.uleb128 0xe
	.uleb128 0x3a
	.uleb128 0x21
	.sleb128 1
	.uleb128 0x3b
	.uleb128 0x21
	.sleb128 5
	.uleb128 0x39
	.uleb128 0xb
	.uleb128 0x49
	.uleb128 0x13
	.uleb128 0x2
	.uleb128 0x18
	.byte	0
	.byte	0
	.uleb128 0x4
	.uleb128 0x11
	.byte	0x1
	.uleb128 0x25
	.uleb128 0xe
	.uleb128 0x13
	.uleb128 0xb
	.uleb128 0x3
	.uleb128 0x1f
	.uleb128 0x1b
	.uleb128 0x1f
	.uleb128 0x11
	.uleb128 0x1
	.uleb128 0x12
	.uleb128 0x6
	.uleb128 0x10
	.uleb128 0x17
	.byte	0
	.byte	0
	.uleb128 0x5
	.uleb128 0x26
	.byte	0
	.uleb128 0x49
	.uleb128 0x13
	.byte	0
	.byte	0
	.uleb128 0x6
	.uleb128 0x24
	.byte	0
	.uleb128 0xb
	.uleb128 0xb
	.uleb128 0x3e
	.uleb128 0xb
	.uleb128 0x3
	.uleb128 0x8
	.byte	0
	.byte	0
	.uleb128 0x7
	.uleb128 0x2e
	.byte	0x1
	.uleb128 0x3f
	.uleb128 0x19
	.uleb128 0x3
	.uleb128 0xe
	.uleb128 0x3a
	.uleb128 0xb
	.uleb128 0x3b
	.uleb128 0x5
	.uleb128 0x39
	.uleb128 0xb
	.uleb128 0x27
	.uleb128 0x19
	.uleb128 0x49
	.uleb128 0x13
	.uleb128 0x3c
	.uleb128 0x19
	.uleb128 0x1
	.uleb128 0x13
	.byte	0
	.byte	0
	.uleb128 0x8
	.uleb128 0x5
	.byte	0
	.uleb128 0x49
	.uleb128 0x13
	.byte	0
	.byte	0
	.uleb128 0x9
	.uleb128 0x18
	.byte	0
	.byte	0
	.byte	0
	.uleb128 0xa
	.uleb128 0x2e
	.byte	0x1
	.uleb128 0x3f
	.uleb128 0x19
	.uleb128 0x3
	.uleb128 0xe
	.uleb128 0x3a
	.uleb128 0xb
	.uleb128 0x3b
	.uleb128 0xb
	.uleb128 0x39
	.uleb128 0xb
	.uleb128 0x11
	.uleb128 0x1
	.uleb128 0x12
	.uleb128 0x6
	.uleb128 0x40
	.uleb128 0x18
	.uleb128 0x7c
	.uleb128 0x19
	.byte	0
	.byte	0
	.uleb128 0xb
	.uleb128 0x34
	.byte	0
	.uleb128 0x3
	.uleb128 0x8
	.uleb128 0x3a
	.uleb128 0xb
	.uleb128 0x3b
	.uleb128 0xb
	.uleb128 0x39
	.uleb128 0xb
	.uleb128 0x49
	.uleb128 0x13
	.uleb128 0x2
	.uleb128 0x18
	.byte	0
	.byte	0
	.byte	0
	.section	.debug_aranges,"",@progbits
	.long	0x1c
	.value	0x2
	.long	.Ldebug_info0
	.byte	0x4
	.byte	0
	.value	0
	.value	0
	.long	.Ltext0
	.long	.Letext0-.Ltext0
	.long	0
	.long	0
	.section	.debug_line,"",@progbits
.Ldebug_line0:
	.section	.debug_str,"MS",@progbits,1
.LASF9:
	.string	"long long int"
.LASF2:
	.string	"unsigned int"
.LASF16:
	.string	"GNU C17 11.3.0 -m32 -mtune=generic -march=i686 -g -fasynchronous-unwind-tables -fstack-protector-strong -fstack-clash-protection"
.LASF6:
	.string	"long unsigned int"
.LASF10:
	.string	"long long unsigned int"
.LASF3:
	.string	"char"
.LASF4:
	.string	"unsigned char"
.LASF13:
	.string	"main"
.LASF11:
	.string	"long int"
.LASF5:
	.string	"short unsigned int"
.LASF12:
	.string	"printf"
.LASF14:
	.string	"ret_cnt"
.LASF15:
	.string	"test"
.LASF8:
	.string	"short int"
.LASF7:
	.string	"signed char"
	.section	.debug_line_str,"MS",@progbits,1
.LASF0:
	.string	"reg7.c"
.LASF1:
	.string	"/home/cccmmf/\346\223\215\344\275\234\347\263\273\347\273\237/\346\223\215\344\275\234\347\263\273\347\273\237\347\234\237\350\261\241\350\277\230\345\216\237/chap6/page291"
	.hidden	__stack_chk_fail_local
	.ident	"GCC: (Ubuntu 11.3.0-1ubuntu1~22.04.1) 11.3.0"
	.section	.note.GNU-stack,"",@progbits
