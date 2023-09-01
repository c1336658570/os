

prog_no_arg.o:prog_no_arg.c
	gcc -Wall -c -g -fno-builtin -W -Wstrict-prototypes -Wmissing-prototypes -Wsystem-headers -I ../lib -o prog_no_arg.o prog_no_arg.c -m32

prog_no_arg:prog_no_arg.o
	ld  -e main prog_no_arg.o ../build/string.o ../build/syscall.o ../build/stdio.o ../build/assert.o -o prog_no_arg -melf_i386

no_arg:prog_no_arg

#gcc -Wall -c -fno-builtin -W -Wstrict-prototypes -Wmissing-prototypes \
#   -Wsystem-headers -I ../lib -o prog_no_arg.o prog_no_arg.c
#ld -e main prog_no_arg.o ../build/string.o ../build/syscall.o\
#   ../build/stdio.o ../build/assert.o -o prog_no_arg
#dd if=prog_no_arg of=/home/work/my_workspace/bochs/hd60M.img \
#   bs=512 count=10 seek=300 conv=notrunc


start.o:start.S
	nasm -f elf ./start.S -o ./start.o -g

simple_crt.a:start.o
	ar rcs simple_crt.a ../build/string.o ../build/syscall.o \
  ../build/stdio.o ../build/assert.o ./start.o

prog_arg.o:prog_arg.c
	gcc -Wall -c -g -fno-builtin -W -Wstrict-prototypes -Wmissing-prototypes -Wsystem-headers -I ../lib/ -I ../lib/user -I ../fs prog_arg.c -o prog_arg.o -m32 -fno-stack-protector

prog_arg:prog_arg.o simple_crt.a
	ld prog_arg.o simple_crt.a -o prog_arg -melf_i386

arg:prog_arg

all:prog_arg prog_no_arg

image:prog_arg prog_no_arg

clean:
	rm -rf prog_no_arg *.o simple_crt.a prog_arg

.PHONY:
	no_arg arg all clean

#nasm -f elf ./start.S -o ./start.o
#ar rcs simple_crt.a ../build/string.o ../build/syscall.o \
#   ../build/stdio.o ../build/assert.o ./start.o
#gcc -Wall -c -fno-builtin -W -Wstrict-prototypes -Wmissing-prototypes \
#   -Wsystem-headers -I ../lib/ -I ../lib/user -I ../fs prog_arg.c -o prog_arg.o
#ld prog_arg.o simple_crt.a -o prog_arg
#dd if=prog_arg of=/home/work/my_workspace/bochs/hd60M.img \
#   bs=512 count=11 seek=300 conv=notrunc