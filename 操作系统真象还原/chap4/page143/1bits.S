[bits 16]
mov ax, 0x1234    ;B83412
mov eax, 0x1234   ;66B834120000

[bits 32]
mov ax, 0x1234    ;66B83412
mov eax, 0x1234   ;B834120000