%include "lib.inc"

section .data
put_int_buffer dq 0   ;定义8字节缓冲区用于put_int

[bits 32]
section .text

;put_str打印以0结尾的字符串
;输入:栈中参数为打印的字符串的内存首地址
;输出:无
global put_str
put_str:
  ;备份ebx ecx
  push ebx
  push ecx
  xor ecx, ecx
  mov ebx, [esp+12]   ;获取待打印字符串地址 ebx + ecx + 返回地址 = 12
.goon:
  mov cl, [ebx]
  cmp cl, 0           ;处理到字符串尾部，结束
  jz .str_over
  push ecx            ;为put_char函数传递参数
  call put_char
  add esp, 4          ;回收栈空间
  inc ebx             ;让ebx指向下一个字符串
  jmp .goon

.str_over:
  pop ecx
  pop ebx
  ret

;把栈中的1个字符写入光标所在处
global put_char
put_char:
  pushad    ;压栈通用寄存器  入栈顺序：EAX->ECX->EDX->EBX-> ESP-> EBP->ESI->EDI
  ;需要保证gs中为正确的视频段选择子，每次执行该函数均为gs赋值
  mov ax, SELECTOR_VIDEO
  mov gs, ax

;;;;;;;;; 获取当前光标位置 ;;;;;;;;;
  ;光标的值为0-1999（80*25=2000）
  ;获取高8位
  mov dx, 0x3d4     ;索引寄存器
  mov al, 0x0e      ;用于提供光标位置的高8位
  out dx, al
  inc dx            ;通过读写数据端口0x3d5来获得或设置光标位置
  in al, dx         ;得到了光标位置的高8位
  mov ah, al

  ;获取低8位
  mov dx, 0x3d4
  mov al, 0x0f
  out dx, al
  inc dx
  in al, dx

  ;光标存入bx
  mov bx, ax
  ;获取栈中的代打印字符
  mov ecx, [esp + 36]   ;8 * 4 + 返回地址 = 36

  cmp cl, 0x0d       ;CR是0x0d（回车），LF是0x0a（换行）
  jz .is_carriage_return  ;回车
  cmp cl, 0x0a
  jz .is_line_feed        ;换行

  cmp cl, 0x08      ;backspace(BS)
  jz .is_backspace
  jmp .put_other    ;普通字符

.is_backspace:
  dec bx    ;光标退格
  shl bx, 1 ;左移1等于*2,表示光标对应显存中的偏移字节

  mov byte [gs:bx], 0x20  ;将待删除的字节补为0或空格(0x20)皆可
  inc bx
  mov byte [gs:bx], 0x07
  shr bx, 1 ;bx由于之前已经加1指向属性了，所以它现在已经变成了奇数，通过右移指令将bx右移1位相当于除2取整，余数不要了
  jmp .set_cursor

.put_other:
  shl bx, 1
  
  mov [gs:bx], cl
  inc bx
  mov byte [gs:bx], 0x07
  shr bx, 1
  inc bx
  cmp bx, 2000
  jl .set_cursor      ;若光标值小于2000，表示未写到显存的最后，则去设置新的光标值70
                      ;若超出屏幕字符数大小（2000），则换行处理

.is_line_feed:        ;换行LF(\n)
.is_carriage_return:  ;回车CE(\r)
;如果是CR(\r)，只要把光标移到行首就行了
  xor dx, dx    ;被除数高16位
  mov ax, bx    ;被除数低16位
  mov si, 80    ;除数
  div si        ;对80求模
  sub bx, dx    ;用坐标值减余数，即将光标移到行首，实现回车

.is_carriage_return_end:
  add bx, 80    ;+80等于将光标移动到下一行这个位置
  cmp bx, 2000
.is_line_feed_end:
  jl .set_cursor

;滚屏
.roll_screen:
  cld
  mov ecx, 960  ;2000-80=1920个字符要搬运，共1920*2=3840字节，一次搬4字节，共3840/4=960次

  mov esi, 0xc00b80a0   ;第1行行首
  mov edi, 0xc00b8000   ;第0行行首
  rep movsd

  ;最后一行设置为空白
  mov ebx, 3840
  mov ecx, 80

.cls:
  mov word [gs:ebx], 0x0720 ;0x0720是黑底白字的空格键
  add ebx, 2
  loop .cls
  mov bx, 1920      ;将光标值重置为1920，最后一行的首字符

.set_cursor:
  ;设置光标高8位
  mov dx, 0x03d4    ;索引寄存器
  mov al, 0x0e      ;用于提供光标位置的高8位
  out dx, al
  inc dx            ;通过读写数据端口0x3d5来获得或设置光标位置
  mov al, bh
  out dx, al

  ;设置光标低8位
  dec dx
  mov al, 0x0f
  out dx, al
  inc dx
  mov al, bl
  out dx, al
.put_char_done:
  popad
  ret

global cls_screen
cls_screen:
  pushad
  ;;;;;;;;;;;;;;;
  ;由于用户程序的cpl为3,显存段的dpl为0,故用于显存段的选择子gs在低于自己特权的环境中为0,
  ;导致用户程序再次进入中断后,gs为0,故直接在put_str中每次都为gs赋值. 
  mov ax, SELECTOR_VIDEO	      ;不能直接把立即数送入gs,须由ax中转
  mov gs, ax

  mov ebx, 0
  mov ecx, 80*25
.cls:
  mov word [gs:ebx], 0x0720		  ;0x0720是黑底白字的空格键
  add ebx, 2
  loop .cls 
  mov ebx, 0

.set_cursor:				  ;直接把set_cursor搬过来用,省事
;;;;;;; 1 先设置高8位 ;;;;;;;;
  mov dx, 0x03d4			  ;索引寄存器
  mov al, 0x0e				  ;用于提供光标位置的高8位
  out dx, al
  mov dx, 0x03d5			  ;通过读写数据端口0x3d5来获得或设置光标位置 
  mov al, bh
  out dx, al

;;;;;;; 2 再设置低8位 ;;;;;;;;;
  mov dx, 0x03d4
  mov al, 0x0f
  out dx, al
  mov dx, 0x03d5 
  mov al, bl
  out dx, al
  popad
  ret

;输入:栈中参数为待打印的数字
;输出:在屏幕上打印十六进制数字，并不会打印前缀0x，如打印十进制15时，只会直接打印f，不会是0xf
global put_int
put_int:
  pushad  ;压栈通用寄存器
  mov ebp, esp
  mov eax, [ebp + 4 * 9]    ;4*8+返回地址 = 4*9
  mov edx, eax
  mov edi, 7                ;指定在put_int_buffer中初始的偏移量
  mov ecx, 8                ;32位数字转换成16进制共8位
  mov ebx, put_int_buffer

;将32位数字按照十六进制的形式从低位到高位逐个处理
.16based_4bits:             ;每4位二进制是十六进制数字的1位
  and edx, 0x0000000F
  cmp edx, 9                ;数字0～9和a～f需要分别处理成对应的字符
  jg .is_A2F
  add edx, '0'              ;ASCII码是8位大小｡ add求和操作后，edx低8位有效
  jmp .store
.is_A2F:
  sub edx, 10
  add edx, 'A'

;将每一位数字转换成对应的字符后，按照类似“大端”的顺序存储到缓冲区put_int_buffer
;高位字符放在低地址，低位字符要放在高地址
.store:
  mov [ebx + edi], dl
  dec edi
  shr eax, 4
  mov edx, eax
  loop .16based_4bits

;现在put_int_buffer中已全是字符，打印之前把高位连续的字符去掉，比如把字符000123变成123
.ready_to_print:
  inc edi         ;此时edi减为-1(0xffffffff)，加1使其为0
.skip_prefix_0:
  cmp edi, 8        ;全部比较完了，代表全0
  je .full0
;找出连续0
.go_on_skip:
  mov cl, [put_int_buffer + edi]
  inc edi
  cmp cl, '0'
  je .skip_prefix_0     ;继续判断下一位字符是否为字符0（不是数字0）
  dec edi               ;edi在上面的inc操作中指向了下一个字符若当前字符不为'0',要使edi减1恢复指向当前字符
  jmp .put_each_num

.full0:
  mov cl, '0'           ;全为0只打印0
.put_each_num:
  push ecx
  call put_char
  add esp, 4
  inc edi
  mov cl, [put_int_buffer + edi]
  cmp edi, 8
  jl .put_each_num
  popad
  ret

global set_cursor
set_cursor:
  pushad
  mov bx, [esp+36]
;;;;;;; 1 先设置高8位 ;;;;;;;;
  mov dx, 0x03d4			  ;索引寄存器
  mov al, 0x0e				  ;用于提供光标位置的高8位
  out dx, al
  mov dx, 0x03d5			  ;通过读写数据端口0x3d5来获得或设置光标位置 
  mov al, bh
  out dx, al

;;;;;;; 2 再设置低8位 ;;;;;;;;;
  mov dx, 0x03d4
  mov al, 0x0f
  out dx, al
  mov dx, 0x03d5 
  mov al, bl
  out dx, al
  popad
  ret
