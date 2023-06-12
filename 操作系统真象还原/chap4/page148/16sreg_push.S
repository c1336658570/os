;           下一条指令的机器码    当前esp值
section loader vstart=0x900
mov sp,0x900    ;以下sp均减2
push cs  ;  0x0e                0x900 
push ds  ;  0x1e                0x8fe
push es  ;  0x06                0x8fc
jmp $    ;  0xebfe              0x8fa
