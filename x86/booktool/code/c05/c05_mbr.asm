;本程序将字符串"Label offset:"输出到显卡，并计算标号number的偏移量，将number的偏移量和D输出到显卡     
;主引导扇区被加载到内存为0x7c00处，BIOS执行完跳转到主引导扇区后CS=0x0000，IP=0x7c00

         mov ax,0xb800                 ;指向文本模式的显示缓冲区
         mov es,ax

         ;以下显示字符串"Label offset:"
         mov byte [es:0x00],'L'
         mov byte [es:0x01],0x07    ;0x7c00是显示的字符(L)的属性，如字体颜色和背景颜色
         mov byte [es:0x02],'a'
         mov byte [es:0x03],0x07
         mov byte [es:0x04],'b'
         mov byte [es:0x05],0x07
         mov byte [es:0x06],'e'
         mov byte [es:0x07],0x07
         mov byte [es:0x08],'l'
         mov byte [es:0x09],0x07
         mov byte [es:0x0a],' '
         mov byte [es:0x0b],0x07
         mov byte [es:0x0c],"o"
         mov byte [es:0x0d],0x07
         mov byte [es:0x0e],'f'
         mov byte [es:0x0f],0x07
         mov byte [es:0x10],'f'
         mov byte [es:0x11],0x07
         mov byte [es:0x12],'s'
         mov byte [es:0x13],0x07
         mov byte [es:0x14],'e'
         mov byte [es:0x15],0x07
         mov byte [es:0x16],'t'
         mov byte [es:0x17],0x07
         mov byte [es:0x18],':'
         mov byte [es:0x19],0x07

         mov ax,number                 ;取得标号number的偏移地址,被除数的低16位
         mov bx,10                  ;除数放入bx寄存器

         ;设置数据段的基地址
         mov cx,cs            ;0x0000->cx
         mov ds,cx            ;cx->ds

         ;求个位上的数字
         mov dx,0     ;被除数的高16位全为0（被除数为dx:ax）
         div bx       ;dx:ax除以bx商在ax中,余数在dx中
         mov [0x7c00+number+0x00],dl   ;由于余数肯定比10小,所以可以从dl中取得,保存个位上的数字
         ;主引导扇区代码会被加载到内存地址为0x7c00处开始执行,而不是0x0000,CS = 0x0000,IP = 0x7c00,DS不会改变为0x7c00,而是一直是0x0000,实际访问内存时应该加上0x7c00
         ;求十位上的数字
         xor dx,dx ;清空dx寄存器,因为商已经在ax中了
         div bx
         mov [0x7c00+number+0x01],dl   ;保存十位上的数字

         ;求百位上的数字
         xor dx,dx
         div bx
         mov [0x7c00+number+0x02],dl   ;保存百位上的数字

         ;求千位上的数字
         xor dx,dx
         div bx
         mov [0x7c00+number+0x03],dl   ;保存千位上的数字

         ;求万位上的数字 
         xor dx,dx
         div bx
         mov [0x7c00+number+0x04],dl   ;保存万位上的数字

         ;以下用十进制显示标号的偏移地址
         mov al,[0x7c00+number+0x04]
         add al,0x30
         mov [es:0x1a],al
         mov byte [es:0x1b],0x04       ;黑底红字无闪烁,无加亮
         
         mov al,[0x7c00+number+0x03]
         add al,0x30
         mov [es:0x1c],al
         mov byte [es:0x1d],0x04 
         
         mov al,[0x7c00+number+0x02]
         add al,0x30
         mov [es:0x1e],al
         mov byte [es:0x1f],0x04

         mov al,[0x7c00+number+0x01]
         add al,0x30
         mov [es:0x20],al
         mov byte [es:0x21],0x04

         mov al,[0x7c00+number+0x00]
         add al,0x30
         mov [es:0x22],al
         mov byte [es:0x23],0x04
         
         mov byte [es:0x24],'D'
         mov byte [es:0x25],0x07       ;黑底白字,表示显示的数字是十进制
          
   infi: jmp near infi                 ;无限循环
      
  number db 0,0,0,0,0
  
  times 203 db 0 ;声明203个字节空洞,times用于重复它后面的指令若干次
            db 0x55,0xaa ;采用dw的话要写成dw 0xaa55