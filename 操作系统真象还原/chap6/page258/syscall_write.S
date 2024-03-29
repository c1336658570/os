section .data
str_c_lib: db "c library says:hello",0x0a		;0xa为LFASCII码,这样就会输出一个换行符。
str_c_lib_len equ $-str_c_lib

str_syscall: db "syscall sys:hi",0x0a
str_syscall_len equ $-str_syscall

section .text
global _start
_start:
;;;;;;;;;;;;; 方式1:模拟C语言中系统调用库函数write ;;;;;;;;;;;;;
	push str_c_lib_len		;按照C调用约定压入参数
	push str_c_lib
	push 1

	call simu_write				;调用下面定义的simu_write
	add esp,12						;回收栈空间

;;;;;;;;;;;;; 方式2:跨过库函数，直接进行系统调用 ;;;;;;;;;;;;;
	mov eax,4							;第4号子功能是write系统调用（不是C库函数write）
	mov ebx,1
	mov ecx,str_syscall
	mov edx,str_syscall_len
	int 0x80							;发起中断，通知Linux完成请求的功能

;;;;;;;;;;;;; 退出程序 ;;;;;;;;;;;
	mov eax,1							;第1号子功能是exit
	int 0x80							;发起中断，通知 Linux 完成请求的功能

;;;;;;;下面自定义的simu_write用来模拟C库中系统调用函数write
;;;;;;这里模拟它的实现原理
simu_write:
	push ebp							;备份 ebp
	mov ebp,esp
	mov eax,4							;第4号子功能是write系统调用（不是C库函数write）
	mov ebx,[ebp+8]				;第1个参数
	mov ecx,[ebp+12]			;第2个参数
	mov edx,[ebp+16]			;第3个参数
	int 0x80							;发起中断，通知Linux完成请求的功能
	pop ebp								;恢复ebp
	ret