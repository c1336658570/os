;比较ax和bx内容,当ax内容大于bx内容时,转移到lbb执行,ax等于bx时,转移到lbz,ax小于bx,转移到lbl

mov ax, 0x1234
mov bx, 0x4567

cmp ax, bx
je lbz
cmp ax, bx
jg  lbb
cmp ax, bx
jl lbl
lbb:
lbz:
lbl: