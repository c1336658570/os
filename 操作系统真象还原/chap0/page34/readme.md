gcc -E main.c
通过查看预处理后的内容发现include功能只是将文件搬运过来，而且和文件名文件后缀无关，
可以任意起一个名字，然后将其include进来，include就会把文件内容搬过来，可以在.h文件中定义函数，因为include会
将文件内容般过来，也就意味着include会直接将那个函数搬过来

gcc -o main.bin -g main.c
./main.bin
echo "====================================================="

gcc -o main.bin -g main.c -v
-v 参数会将编译、链接两个过程详细地打印出来。通过查看-v显示的信息，可以去看如下这些内容，例如：调用cc1，调用as，调用collect2，LIBRARY_PATH
这是个库路径变量，里面存储的是库文件所在的所有路径，这就是编译器所说的标准库的位置，自动到该变量所包含的路径中去找库文件。
链接器 collect2 的参数除了有咱们的 main.c 生成的目
标文件 cc0yJGmy.o 以外，还有以下这几个以 crt 开头的目标文件：crt1.o，crti.o，crtbegin.o，crtend.o，crtn.o。
crt 是什么？CRT，即 C Run-Time library，是 C 运行时库。

gcc 内部也要将 C 代码经过编译、汇编、链接三个阶段。
（1）编译阶段是将 C 代码翻译成汇编代码，由最上面的框框中的 C 语言编译器 cc1 来完成，它将 C
代码文件 main.c 翻译成汇编文件 ccymR62K.s。
（2）汇编阶段是将汇编代码编译成目标文件，用第二个框框中的汇编语言编译器 as 完成，as 将汇编
文件 ccymR62K.s 编译成目标文件 cc0yJGmy.o。
（3）链接阶段是将所有使用的目标文件链接成可执行文件，这是用左边最下面框框中的链接器
collect2 来完成的，它只是链接命令 ld 的封装，最终还是由 ld 来完成，在这一堆.o 文件中，有咱们上面的
目标文件 cc0yJGmy.o。

#ltrace -S ./mian.bin  #使用ltrace跟踪程序运行时调用的库函数  -S参数查看系统调用  
#或使用trace ./main.bin 查看系统调用的封装函数  用-e trace=write来限制只看write系统调用