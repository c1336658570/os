;下一条指令           下一条指令机器码    当前esp值
section loader vstart=0x900
mov sp,0x900        ;bc0009           0x00007c00
push byte 0x7       ;6a07             0x00000900
push word 0x8       ;6a08             0x000008fe  0x900-0x8fe=2
;该指令66是操作数反转前缀，将当前的默认操作数反转成32位
;09是操作数，6a 是操作码，66 是操作数大小反转前缀
push dword 0x90     ;666a09           0x000008fc  0x8fe-0x8fc=2
jmp $               ;ebfe             0x000008f8  0x8fc-0x8f8=4
