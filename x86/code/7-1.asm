;�޸�Դ����31-37��,��loop���,CX��Ҫ����ѭ������ҲҪ��Ϊ����
         jmp near start
	
 message db '1+2+3+...+100='
        
 start:
         mov ax,0x7c0           ;�������ݶεĶλ���ַ 
         mov ds,ax

         mov ax,0xb800          ;���ø��Ӷλ�ַ����ʾ������
         mov es,ax

         ;������ʾ�ַ��� 
         mov si,message          
         mov di,0
         mov cx,start-message
     @g:
         mov al,[si]
         mov [es:di],al
         inc di
         mov byte [es:di],0x07
         inc di
         inc si
         loop @g

         ;���¼���1��100�ĺ� 
         xor ax,ax
         mov cx,100
     @f:
         add ax,cx
         loop @f

         ;���¼����ۼӺ͵�ÿ����λ 
         xor cx,cx              ;���ö�ջ�εĶλ���ַ
         mov ss,cx
         mov sp,cx

         mov bx,10
         xor cx,cx ;��cx���㣬�����ۼ�һ���ж��ٸ���λ
     @d:
         inc cx    ;cx��1��ʾ�ֽ��������λ+1
         xor dx,dx
         div bx
         or dl,0x30   ;dl��������,��λ�򣬴˴�Ч����ͬ��+30
         push dx
         cmp ax,0
         jne @d

         ;������ʾ������λ 
     @a:
         pop dx
         mov [es:di],dl
         inc di
         mov byte [es:di],0x07
         inc di
         loop @a    ;��cx����ѭ����������һ���ֽ������λ��loopÿִ��һ��cx��1
       
         jmp near $ 
       

times 510-($-$$) db 0
                 db 0x55,0xaa