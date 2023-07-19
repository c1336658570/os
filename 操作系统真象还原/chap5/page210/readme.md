用来证实第第一个默认运行的程序不是main函数，而是_start，然后_start会调用main，也可以直接定义_start函数，然后让_start调用main。也用来证实gcc编译链接程序会添加许多额外信息

gcc -c -o start.o start.c -m32
ld start.o -Ttext 0xc0001500 -o start.bin -melf_i386

gcc -c -o main.o main.c -m32
ld main.o -Ttext 0xc0001500 -e main -o main.bin -melf_i386

