;nasm -f elf 1.asm -o 1.o 
;nasm -f elf 2.asm -o 2.o  
;ld -melf_i386 1.o 2.o -o 12 
;readelf -e ./12 
section .bss
    resb 2*32

section file1data   ;自定义的数据段，未使用传统的.data
    strHello db "hello,youyifeng!",0Ah
    STRLEN equ $-strHello
    
section file1text   ;自定义代码段，未使用传统的.text
    extern print    ;声明此函数在别的文件中，告诉编译器编译时找不到此符号也没关系，链接时会找到
    global _start   ;链接器把_start当作程序入口
_start:
    push STRLEN     ;传入参数，字符串长度
    push strHello   ;传入参数，代打印字符串
    call print      ;此函数定义在2.asm

    mov ebx,0       ;返回值4
    mov eax,1       ;系统调用号1:sys_exit
    int 0x80        ;系统调用
