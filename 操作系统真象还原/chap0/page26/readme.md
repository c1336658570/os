nasm -f elf 1.asm -o 1.o
nasm -f elf 2.asm -o 2.o
ld -melf_i386 1.o 2.o -o 12

./12    运行程序

编译链接结束后，使用readelf看文件格式，主要看魔术，节头（Section Headers），程序头（Program Headers），
段节（Section to Segment mapping），观察这几点内容
readelf -e ./12
