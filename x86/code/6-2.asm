;6-2课后题，求正数和负数有多少个

jmp near start

data1   db 0x05, 0xff, 0x80, 0xf0, 0x97, 0x30
data2   dw 0x90, 0xfff0, 0xa0, 0x1235, 0x2f, 0xc0, 0xc5bc
number:

start:
        ;设置ds段寄存器
        mov ax, 0x07c0
        mov ds, ax 
        ;设置es段寄存器
        mov ax, 0xb800
        mov es, ax

        ;设定循环次数
        mov cx, data2 - data1
        ;bx清零,记录正数个数
        xor bx, bx
        ;dx清零,记录负数的个数
        xor dx, dx
        ;设置基地址
        mov bp, data1

digtal1:
        xor ax, ax ;清空ax
        mov al, [ds:bp]  ;bp默认是ss<<4位+bp形成物理地址,所以需要显示指出访问ds
        inc bp
        cmp al, 0
        jge pos_num1
        inc dx
        jmp near looop1
pos_num1: 
        inc bx

looop1:
        loop digtal1   

        ;设定循环次数
        mov cx, (number - data2) / 2  ;除2是因为以字为单位
        ;设置基地址
        mov bp, data2

digtal2:
        xor ax, ax ;清空ax
        mov ax, [ds:bp]  ;bp默认是ss<<4位+bp形成物理地址,所以需要显示指出访问ds
        add bp, 0x02
        cmp ax, 0
        jge pos_num2
        inc dx
        jmp near looop2
pos_num2: 
        inc bx

looop2:
        loop digtal2

        ;输出
        add bl, 0x30
        add dl, 0x30
        mov [es:0x00], bl 
        mov byte[es:0x01], 0x07
        mov byte [es:0x02], ' '
        mov byte [es:0x03], 0x07
        mov [es:0x04], dl
        mov byte [es:0x05], 0x07

        jmp near $

times 510 - ($ - $$) db 0
        db 0x55, 0xaa

    
