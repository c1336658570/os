;nasm -f elf 1.asm -o 1.o 
;nasm -f elf 2.asm -o 2.o  
;ld -melf_i386 1.o 2.o -o 12 
;readelf -e ./12 
section .bss
    resb 2*32

section file1data   ;自定义的数据段，未使用传统的.data
    strHello db "hello,youyifeng!",0Ah
    STRLEN equ $-strHello
    
section file1text
    extern print
    global _start
_start:
    push STRLEN
    push strHello
    call print

    mov ebx,0
    mov eax,1
    int 0x80
