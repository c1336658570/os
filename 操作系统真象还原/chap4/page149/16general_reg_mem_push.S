;                 下一条的指令的机器码    当前 esp 值
section loader vstart=0x900
mov sp, 0x900
push ax         ; 50                   0x900
;66反转操作数大小
push eax        ; 6650                 0x8fe
push word [0x1234]  ;ff363412          0x8fa
push dword [0x1234] ;66ff363412        0x8f8
jmp $           ; ebfe                 0x8f4