section .text
    mov eax,0x10
    jmp $

section file2data
    file2var db 3
    
section file2text
    global print        ;导出printf，供其他模块使用
print:
    mov edx,[esp+8]     ;字符串长度
    mov ecx,[esp+4]     ;字符串
    mov ebx,1
    mov eax,4           ;sys_write
    int 0x80            ;系统调用
    ret
