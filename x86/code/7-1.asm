;修改源代码31-37行,用loop求和,CX即要控制循环次数也要作为加数
         jmp near start
	
 message db '1+2+3+...+100='
        
 start:
         mov ax,0x7c0           ;设置数据段的段基地址 
         mov ds,ax

         mov ax,0xb800          ;设置附加段基址到显示缓冲区
         mov es,ax

         ;以下显示字符串 
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

         ;以下计算1到100的和 
         xor ax,ax
         mov cx,100
     @f:
         add ax,cx
         loop @f

         ;以下计算累加和的每个数位 
         xor cx,cx              ;设置堆栈段的段基地址
         mov ss,cx
         mov sp,cx

         mov bx,10
         xor cx,cx ;将cx清零，用来累计一共有多少个数位
     @d:
         inc cx    ;cx加1表示分解出来的数位+1
         xor dx,dx
         div bx
         or dl,0x30   ;dl保存余数,按位或，此处效果等同于+30
         push dx
         cmp ax,0
         jne @d

         ;以下显示各个数位 
     @a:
         pop dx
         mov [es:di],dl
         inc di
         mov byte [es:di],0x07
         inc di
         loop @a    ;由cx决定循环次数，即一共分解出多少位，loop每执行一次cx减1
       
         jmp near $ 
       

times 510-($-$$) db 0
                 db 0x55,0xaa