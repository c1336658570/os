start.o:start.S
	nasm -f elf ./start.S -o ./start.o -g

simple_crt.a:start.o
	ar rcs simple_crt.a ../build/string.o ../build/syscall.o \
  ../build/stdio.o ../build/assert.o ./start.o

prog_arg.o:prog_arg.c
	gcc -Wall -c -fno-builtin -W -Wstrict-prototypes -Wmissing-prototypes \
	-Wsystem-headers -m32 -I ../lib/ -I ../lib/user -I ../fs prog_arg.c -o prog_arg.o -fno-stack-protector
prog_arg:prog_arg.o simple_crt.a
	ld prog_arg.o simple_crt.a -o prog_arg -melf_i386

arg:prog_arg



all:prog_arg  

image:prog_arg progno_arg 

clean:
	rm -rf *.o simple_crt.a prog_arg 

.PHONY:
	no_arg arg all clean
