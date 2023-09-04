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





prog_no_arg.o:prog_no_arg.c
	gcc $(CFLAGS) $(LIBS) -o prog_no_arg.o prog_no_arg.c

prog_no_arg:prog_no_arg.o simple_crt.a
	ld prog_no_arg.o simple_crt.a -o prog_no_arg -melf_i386

no_arg:prog_no_arg




prog_arg.o:prog_arg.c
	gcc $(CFLAGS) $(LIBS) prog_arg.c -o prog_arg.o 

prog_arg:prog_arg.o simple_crt.a
	ld prog_arg.o simple_crt.a -o prog_arg -melf_i386

arg:prog_arg




BIN=cat


$(BIN).o:$(BIN).c
	gcc $(CFLAGS) $(LIBS) -o $(BIN).o $(BIN).c

$(BIN):$(BIN).o simple_crt.a
	ld $(BIN).o simple_crt.a -o $(BIN) -melf_i386

cat_:$(BIN)


prog_pipe.o:prog_pipe.c
	gcc $(CFLAGS) $(LIBS) -o $@ $^

prog_pipe:prog_pipe.o simple_crt.a
	ld $^ -o $@ -melf_i386

prog_pipe_:prog_pipe


all:prog_arg prog_no_arg $(BIN) prog_pipe

image:prog_arg prog_no_arg $(BIN) prog_pipe

clean:
	rm -rf *.o simple_crt.a prog_no_arg prog_arg cat prog_pipe

.PHONY:
	no_arg arg all clean
