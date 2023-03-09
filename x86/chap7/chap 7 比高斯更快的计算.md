## chap 7 比高斯更快的计算

栈和代码段、数据段和附加段一样，栈被定义为一个内存段，叫栈段，由段寄存器SS指向。psuh和pop是栈的两个操作。压栈和出栈只能在一端进行，需要栈指针寄存器SP来指示下一个数据应该压入栈内的什么位置，或者 数据从哪里找。

定义栈需要俩操作，初始化段寄存器SS和栈指针SP。

<img src="/home/cccmmf/操作系统/x86/chap7/屏幕截图 2023-03-09 004013.png" alt="屏幕截图 2023-03-09 004013" style="zoom:75%;" />

or是逻辑或，or指令的目的操作数可以是8位，16位通用寄存器或包含8/16位实际操作数的内存单元，源操作数可以是与目的操作数数据宽度相同的内存单元、寄存器、立即数。不允许源操作数和目的操作数都为内存单元。or会导致OF和CF清零，SF、ZF 、PF位按结果而定，AF位未定义。

and指令，两个操作数都应当是字节或字。目的操作数可以是8位，16位通用寄存器或包含8/16位实际操作数的内存单元，源操作数可以是与目的操作数数据宽度相同的内存单元、寄存器、立即数。不允许源操作数和目的操作数都为内存单元。and执行后，OF和CF清零，SF、ZF 、PF位按结果而定，AF位未定义。

push只接收16位操作数，可以是内存或寄存器，8086只能压入一个字。其后的32位、64位处理器允许压入字、双字或者四字，因此关键字必不可少。

以下指令在8086非法：

push al

push byte [label_a]

处理器执行push时首先将SP减去操作数的字长（16位处理器是2，以字节为单位），然后，把压入栈的数据存放到逻辑地址SS：SP指向的内存位置。

<img src="/home/cccmmf/操作系统/x86/chap7/屏幕截图 2023-03-09 005602.png" alt="屏幕截图 2023-03-09 005602" style="zoom:75%;" />

pop是将一个字弹出，操作数可以是16位内存单元或寄存器，并将SP内容加上操作数字长。pop不影响任何标志位。

如下代码可以代替push：

sub sp, 2

mov bx, sp

mov [ss:bx], ax

如下代码可以代替pop

mov bx, sp

mov ax, [ss:bx]

add sp, 2

<img src="/home/cccmmf/操作系统/x86/chap7/屏幕截图 2023-03-09 010726.png" alt="屏幕截图 2023-03-09 010726" style="zoom:75%;" />



print-stack查看栈的状态，默认显示16个字。



#### 寄存器寻址

mov ax, cx

add bx, 0xf000

inc dx

#### 立即寻址

又名立即数寻址，指令操作数是一个数。

add bx, 0xf000

mov dx, label_a

#### 内存寻址

所谓内存寻址，就是寻找偏移地址，这称为有效地址（Effective Address，EA）。就是如何在指令中提供偏移地址，供处理器访问内存时使用。

##### 1.直接寻址

操作数是一个偏移地址

mov ax, [0x5c0f]	源操作数直接寻址

add word [0x0230], 0x5000	目的操作数直接寻址

xor byte [es:label_b], 0x05	目的操作数直接寻址

##### 2.基址寻址

buffer dw 0x20, 0x100, 0x0f, 0x300, 0xff00

直接寻址：

inc word [buffer]

inc word [buffer+2]

inc word [buffer+4]

基址寻址就是指令的地址部分使用BX或BP来提供偏移地址，比如：

mov [bx], dx	它默认段寄存器是DS，mov [ds:bx], dx

add byte [bx], 0x55

使用基址寻址导致程序简洁

mov bx, buffer

mov cx, 4

lpinc:

​	inc word [bx]

​	add bx, 2

​	loop lpinc

基址寄存器也可以是bp

mov ax, [bp]	它默认寄存器是SS，经常用来访问段。mov ax, [ss:bp]

<img src="/home/cccmmf/操作系统/x86/chap7/屏幕截图 2023-03-09 014520.png" alt="屏幕截图 2023-03-09 014520" style="zoom:75%;" />

基址寻址允许在基地址寄存器基础上添加偏移量

mov dx, [bp-2]

也适用于基址寄存器bx

xor bx, bx

mov cx, 4

lpinc:

​	inc word [bx + buffer]

​	add bx, 2

​	loop lpinc

##### 3.变址寻址

类似基址寻址，使用变址寄存器SI和DI，默认访问DS段指向的数据段。

mov [si], dx

add ax, [di]

add ax, [di]

xor word [si], 0x8000

变址寻址同样允许带偏移量

mov [si+0x100], al

add byte [di+label_a], 0x80

##### 4.基址变址寻址

string db 'abcdefghijklmnopqrstuvwxyz'

目的：将26个字符反向排列

1.使用栈

mov cx, 26

mov bx, string （基地址）

lppush:

​	mov al, [bx]

​	push ax

​	inc bx

​	loop lppush

​	

​	mov cx, 26

​	mov bx, string

lppop:

​	pop ax

​	mov [bx], al

​	inc bx

​	loop lppop

2.使用基址寄存器（BX或BP）和变址寄存器（DX或SI）

mov ax, [bx+si]

add word [bx + di], 0x3000

采用基址变址排序代码如下：

​	mov bx, string

​	mov si, 0

​	mov di, 25

order:

​	mov ah, [bx + si]

​	mov al, [bx +di]

​	mov [bx + si], al

​	mov [bx + di], ah

​	inc si

​	dec di

​	cmp si, di

​	jl order

基址加变址加偏移量也可以

mov [bx + si + 0x100], al

and byte [bx + di + label_a], 0x80

