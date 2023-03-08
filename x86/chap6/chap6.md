## chap6相同的功能，不同的代码

0000:7C00和07c0:0000是同一个地址

<img src="/home/cccmmf/操作系统/x86/chap6/2023-03-07_12-29.png" alt="2023-03-07_12-29" style="zoom:75%;" />

movsb和movsw指令通常用于把数据从内存中的一个地方批量传送（复制)到另一个地方，处理器把他们看成数据串。movsb以字节为单位，movsw以字为单位传送。

movsb和movsw执行时，源数据串段地址有DS指定，偏移地址由SI指定，DS：SI。要传送的目的地址由ES：DI指定，传送的字节数或字数由CX指定。还要指定正向传送还是反向传送，正向传送是指传送操作方向是从内存区域的低地址端到高地址端，反向传送相反。正向传送时，每传送一个字节或者一个字时，SI和DI加1或者加2。反向时SI和DI减去1或者减去2。每传送一次，CX的内容总是减1。

<img src="/home/cccmmf/操作系统/x86/chap6/2023-03-07_12-15.png" alt="2023-03-07_12-15" style="zoom:75%;" />

ZF为零标志，ZF为1表示计算结果为0。DF（Direction Flag）是方向标志，通过这一位清零或者置1就可以控制movsb和movsw的传送方向。

cld为方向清零指令，无操作数，与其相反的为置方向标志指令std。cld将DF清零，以示传送方向为正方向，std则相反。

movsb		表示传送一个字节

movsw		表示传送一个字s

rep movsw 		表示只要CX不为0就重复执行movsw

rep movsb 		表示只要CX不为0就重复执行movsb



loop指令的功能是重复执行一段相同代码，处理器执行时会做两件事

将寄存器CX内容减1

如果CX不为0,转移到指定的位置执行，否则顺序执行后面的指令

例：loop   digit   		机器码为0xE2，后面跟一个字节操作数，也是相对于标号处的偏移量，是在编译阶段，编译器用标号digit所在位置的汇编地址减去loop指令的汇编地址，在减去loop指令的长度得来的

在8086中，如果用寄存器来提供偏移地址只能使用BX,SI,DI,BP,不能使用其他寄存器。所以以下指令都是非法的：

mov [ax], dl

mov [dx], bx

设计时BX最初功能之一就是提供数据访问的基地址，又叫基地址寄存器（Base Address Register）。不能用SP,IP,AX,CX,DX，是规定。AX是累加器（Accumulator），CX是计数器（Counter），DX是数据（Data）寄存器；SI是源索引寄存器（Source Index），DI是目标索引寄存器（Destination Index），用于数据传送，在movsb和movsw已经领略过了。

可以在任何带有内存操作数的指令中使用BX,SI或者DI提供偏移地址。

inc是加1指令，操作数可以是8位或16位寄存器，也可以是字节或者字内存单元。

inc al

inc byte [bx]

inc word [label_a]    段地址在DS中，偏移地址等于标号label_a在编译阶段的汇编地址

dec表示将目标操作数减1,可以跟8位或16位通用寄存器或内存单元

很多算术逻辑操作会影响到符号位SF，比如dec,如果计算结果最高位是0,处理器把SF位置为0,否则SF位置为1



neg指令有一个操作数，可以是8位或16位寄存器，或者内存单元，如：

neg al

neg dx

neg word [label_a]

neg表示用0减去指定的操作数。如果al存的00001000（8）,执行neg al后，al内容变为11111000（-8），如果al内容为11000100（十进制数-60），执行neg al后，al内容为00111100

cbw（Convert Byte to Word）和cwd（Convert Word to Double-word）。

cbw没有操作数，操作码为98,将寄存器AL中的有符号数拓展到整个AX。

cwd也没有操作数，操作码为99，将寄存器AX中的有符号数拓展到DX：AX。

sub是减法指令，目的操作数是8位或16位寄存器，也可以是8位或16位内存单元；源操作数可以是通用寄存器，也可以是内存单元或立即数（不允许俩操作数都是内存单元）。

sub ah, al

sub dx, ax

sub [label_a], ch

sub ah, al等效于

neg al			add ah, al

div严格来说叫无符号数发指令，因为只能工作于无符号数

mov ax, 0x0400
mov bl, 0xf0

div bl		执行后al内容为0x04

无符号角度来看，0x400为1024,0xf0为240.相除后AL商为0x04,完全正确。但从有符号角度看，0xf0为-16,理论上相除后AL结果位0xc0，为-64。

处理器有专门的有符号除法指令idiv。例如：

mov ax 0x0400

mov bl, 0xf0

idiv bl		执行后al内容位0xc0,即-64

idiv除法时需要小心溢出，用0xf0c0除0x10,就是-3904除16,可能会这么做

mov ax, 0xf0c0

mov bl, 0x10

idiv bl

以上位16位二进制除法，结果放到al中。除法结果应该是-244,但是超出了al所能表示的范围，必然因为溢出导致结果出错。如下使用32位除法

xor dx, dx				如此以来导致DX：AX中的数成了正数

mov ax, 0xf0c0

mov bx, 0x10

idiv bl

这依然是错的，十进制数-3904的16位二进制形式和32位二进制形式是不同的。前者是0xf0c0,后者是0xfffff0c0。应该使用cwd将ax内容符号拓展，以下写法正确

mov ax, 0xf0c0

cwd

mov bx, 0x10

idiv bx

执行完后AX内容为0xff0c,即-244

<img src="/home/cccmmf/操作系统/x86/chap6/2023-03-07_19-14.png" alt="2023-03-07_19-14" style="zoom:75%;" />

jns show的意思为，如果未设置符号位，则转移到“show”所在位置执行。

如下图参看代码49行

![2023-03-07_19-28](/home/cccmmf/操作系统/x86/chap6/2023-03-07_19-28.png)



奇偶校验位PF

当运算结果出来后，如果最低8位中，偶数个1,则PF=1,否则PF=0

mov ax, 1000100100101110B   	ax <- 0x892e

xor ax, 3 										结果位0x892d（1000100100101101B）

邮局结果低8位是00101110B，偶数个1,PF=1

mov ah, 00100110B					ah <- 0x26

mov al, 10000001B					al <- 0x81

add ah, al									ah <- 0xa7

ah0xa7(10100111B)，奇数个1,PF = 0

进位标志CF

如果最高位有向前进位或借位，CF = 1，否则CF = 0

mov al, 10000000B			al <- 0x80

add al, al							al <- 0x80

最高位是1产生进位。进位被丢弃，AL中最终结果为0.CF = 1。同时，ZF = 1, PF = 1

借位而导致CF=1：

mov ax, 0

mov ax, 1

CF为1，CF始终记录进位或借位是否发生，但少数指令除外（inc和dec）

溢出标志OF

溢出OF=1,否则OF=0

mov ah, 0xff

add ah, 2

执行后CF = 1，实际上是-1 + 2，OF = 0

mov ah, 0x70

add ah, ah

CF = 0，无符号角度看112+112 = 224,未超过255结果正确。有符号角度看，112+112=-32,明显错误。OF = 1。处理器不知道你进行的是有符号还是无符号运算，它假定做的有符号运算，并设置OF位，如果你做的无符号数运算，忽略OF即可。

<img src="/home/cccmmf/操作系统/x86/chap6/2023-03-07_19-41.png" alt="2023-03-07_19-41" style="zoom:75%;" />



“jcc”不是一条指令，而是一个指令族，根据某些跳进进行转移。比如jns,意思是SF！=1(SF = 0)就转移。js（奸商）是SF=1就转移。条件转移指令的操作数是标号，编译成机器码后，操作数是一个立即数，相对于目标指令的偏移量个。在16位处理器上，偏移量可以是8位（短转移）或者16位（相对近转移）。

jz位ZF标志为1则转移，jnz位ZF=0转移

jo是OF=1转移，jno是OF=0转移

jc是CF=1转移，jnc是CF=0转移

jp（极品）是PF=1转移，jnp是PF=0转移。

转移指令必须出现在影响标志的指令之后，比如：

dec si

jns show

多数时候会遇到标志关系不明显，比如当AX位0x30时转移，或者AX小于0x30转移，处理器提供了cmp指令，两个操作数，目的操作数可以是8位或16位通用寄存器，或8位或16位内存单元。源操作数可以是与目的操作数宽度一致的通用寄存器，内存单元或者立即数，不能同时为内存单元。

cp al, 0x08

cmp dx, bx

cmp [label_a], ax

cmp功能和sub相同，cmp只根据计算结果设置标志位，不保留计算结果。cmp会影响CF、OF、SF、ZF、AF和PF标志位。比较是拿目的操作数和源操作数比较，重点在目的操作数，比如cmp ax, bx，关心的是ax是否等于bx，大于bx，小于bx。ax是被测对象，bx是基准。

<img src="/home/cccmmf/操作系统/x86/chap6/2023-03-07_19-53.png" alt="2023-03-07_19-53" style="zoom:75%;" />

<img src="/home/cccmmf/操作系统/x86/chap6/2023-03-07_19-53_1.png" alt="2023-03-07_19-53_1" style="zoom:75%;" />

jcxz（jump if CX is zero），意思是当CX寄存器内容为0时转移。执行这条指令，处理器先测试CX是否为0,例如：

jcxz show			如果CX为0则转移，否则不转移

jmp near $                   无限循环,nasm编译器提供了$,可以理解为隐藏在当前行行首的标号

times 510-($-$$) db 0               ;重复执行db 0若干次,次数由510-($-$$)得到,出去0x55和0xaa后,剩余的主引导扇区是510个字节,$$nasm编译器提供的另一个标记,代表当前汇编节(段)的起始汇编地址



单步调试指令如果遇到rep movsw会重复执行，直到cx为0才执行下一条

rep movsw等价指令为rep movsw word ptr es:[di] , word ptr ds:[si]

loop指令循环也会导致s一直执行

使用n可以让调试直接跳出循环去执行吓一跳指令。

n对下面无效

<img src="/home/cccmmf/操作系统/x86/chap6/屏幕截图 2023-03-08 205104.png" alt="屏幕截图 2023-03-08 205104" style="zoom:80%;" />

<img src="/home/cccmmf/操作系统/x86/chap6/屏幕截图 2023-03-08 232558.png" alt="屏幕截图 2023-03-08 232558" style="zoom:80%;" />

<img src="/home/cccmmf/操作系统/x86/chap6/屏幕截图 2023-03-08 232620.png" alt="屏幕截图 2023-03-08 232620" style="zoom:38%;" />

info可以查看flags，8086中flags叫做flags，16位，32位处理器中做了拓展，叫eflags，在bochs中应输入info eflags，因为bochs是32位

用法：info 		info  eflags

<img src="/home/cccmmf/操作系统/x86/chap6/屏幕截图 2023-03-08 233059.png" alt="屏幕截图 2023-03-08 233059" style="zoom:38%;" />
