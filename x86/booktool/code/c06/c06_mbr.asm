;本程序和c05_mbr功能相同，只是采用了循环
;将字符串"Label offset:"输出到显卡，并计算标号number的偏移量，将number的偏移量和D输出到显卡

         jmp near start
         
  mytext db 'L',0x07,'a',0x07,'b',0x07,'e',0x07,'l',0x07,' ',0x07,'o',0x07,\
            'f',0x07,'f',0x07,'s',0x07,'e',0x07,'t',0x07,':',0x07
  number db 0,0,0,0,0
  
  start:
         mov ax,0x7c0                  ;设置数据段基地址，0x7c0!!!，而不是0x7c00 
         mov ds,ax
         
         mov ax,0xb800                 ;设置附加段基地址 
         mov es,ax
         
         cld                           ;方向清零指令,将DF(方向标志)清零,表示指令传送是正方向
         mov si,mytext                 ;设置SI寄存器的内容到源串的首地址
         mov di,0                      ;设置目的地的首地址到DI寄存器
         mov cx,(number-mytext)/2      ;设置要批量传送的字节数，实际上等于 13
         rep movsw                     ;单纯的movsw和movsb只执行一次,前缀rep表示CX不为0就重复,每执行一次cx减1
     
         ;得到标号所代表的偏移地址
         mov ax,number
         
         ;计算各个数位
         mov bx,ax
         mov cx,5                      ;循环次数 
         mov si,10                     ;除数 
  digit: 
         xor dx,dx
         div si
         mov [bx],dl                   ;保存数位
         inc bx 
         loop digit            ;每循环一次cx减1
         
         ;显示各个数位
         mov bx,number 
         mov si,4                      
   show:
         mov al,[bx+si]
         add al,0x30
         mov ah,0x04
         mov [es:di],ax
         add di,2                     ;将di加2,指向下一块内存单元
         dec si                       ;将si减1,使下次bx+si指向千位,百位...,dec会影响SF位,dec后最高位为0,则SF为0,否则为1
         jns show                     ;如果未设置符号位,则转到show所在位置执行
         
         mov word [es:di],0x0744      ;在显示各个数位后显示D,0x07是黑底白字,0x44是D 

         jmp near $                   ;无限循环,nasm编译器提供了$,可以理解为隐藏在当前行行首的标号

  times 510-($-$$) db 0               ;重复执行db 0若干次,次数由510-($-$$)得到,出去0x55和0xaa后,剩余的主引导扇区是510个字节,$$nasm编译器提供的另一个标记,代表当前汇编节(段)的起始汇编地址
                   db 0x55,0xaa
       ;此程序没有定义节或段,默认自成一个汇编段,起始地址是0(程序起始处)