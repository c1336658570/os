;求1+...+1000=  计算结果会超过16位小于32位，用普通add无法实现，普通add只能进行8/16位运算
;adc为带进位加法,和add一样，除了加上源操作数，还要加上进位标志CF，用adc和add配合使用，实现16位以上加法

jmp near start

message db '1+2+3+...+1000='

start:
    ;设置数据段基地址
    mov ax, 0x07c0
    mov ds, ax 

    ;设置附加段基地址，到b800处，即显存的位置
    mov ax, 0xb800
    mov es, ax 

    mov si, message
    mov di, 0x00
    mov cx, start - message

  ;将'1+2+3+...+1000=显示到显示器上'
  @a:
    mov al, [si]
    mov [es:di], al
    inc di 
    mov byte [es:di], 0x07
    inc di 
    inc si 
    loop @a

    ;计算'1+2+3+...+1000='
    xor ax, ax 
    xor dx, dx 
    mov cx, 0x01
  @b:
    add ax, cx
    adc dx, 0
    inc cx 
    cmp cx, 1000
    jle @b

    ;计算各个数位
    xor cx, cx 
    mov sp, cx 
    mov ss, cx 
    mov bx, 10  
  @c:
    inc cx
    div bx
    or dl, 0x30
    push dx
    xor dx, dx
    cmp ax, 0
    jne @c  

  ;将各个数位输出
  @d:
    pop dx 
    mov [es:di], dl 
    inc di 
    mov byte [es:di], 0x07
    inc di
    loop @d

    jmp near $
    


times 510 - ($-$$) db 0
db 0x55, 0xaa