start.o:start.S
	nasm -f elf ./start.S -o ./start.o -g

simple_crt.a:start.o
	ar rcs simple_crt.a ../build/string.o ../build/syscall.o \
  ../build/stdio.o ../build/assert.o ./start.o

LIBS=-I ../lib/ -I ../lib/kernel/ -I ../lib/user/ -I \
      ../kernel/ -I ../device/ -I ../thread/ -I \
      ../userprog/ -I ../fs/ -I ../shell/

CFLAGS=-Wall -c -g -fno-builtin -W -Wstrict-prototypes \
      -Wmissing-prototypes -Wsystem-headers -m32 -fno-stack-protector


prog_pipe.o:prog_pipe.c
	gcc $(CFLAGS) $(LIBS) -o $@ $^

prog_pipe:prog_pipe.o simple_crt.a
	ld $^ -o $@ -melf_i386

prog_pipe_:prog_pipe


all:prog_pipe

image:prog_pipe

clean:
	rm -rf *.o simple_crt.a prog_pipe

.PHONY:
	all clean
