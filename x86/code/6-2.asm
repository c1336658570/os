;6-2�κ��⣬�������͸����ж��ٸ�

jmp near start

data1   db 0x05, 0xff, 0x80, 0xf0, 0x97, 0x30
data2   dw 0x90, 0xfff0, 0xa0, 0x1235, 0x2f, 0xc0, 0xc5bc
number:

start:
        ;����ds�μĴ���
        mov ax, 0x07c0
        mov ds, ax 
        ;����es�μĴ���
        mov ax, 0xb800
        mov es, ax

        ;�趨ѭ������
        mov cx, data2 - data1
        ;bx����,��¼��������
        xor bx, bx
        ;dx����,��¼�����ĸ���
        xor dx, dx
        ;���û���ַ
        mov bp, data1

digtal1:
        xor ax, ax ;���ax
        mov al, [ds:bp]  ;bpĬ����ss<<4λ+bp�γ������ַ,������Ҫ��ʾָ������ds
        inc bp
        cmp al, 0
        jge pos_num1
        inc dx
        jmp near looop1
pos_num1: 
        inc bx

looop1:
        loop digtal1   

        ;�趨ѭ������
        mov cx, (number - data2) / 2  ;��2����Ϊ����Ϊ��λ
        ;���û���ַ
        mov bp, data2

digtal2:
        xor ax, ax ;���ax
        mov ax, [ds:bp]  ;bpĬ����ss<<4λ+bp�γ������ַ,������Ҫ��ʾָ������ds
        add bp, 0x02
        cmp ax, 0
        jge pos_num2
        inc dx
        jmp near looop2
pos_num2: 
        inc bx

looop2:
        loop digtal2

        ;���
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

    
