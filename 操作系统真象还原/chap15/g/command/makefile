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

all: prog_no_arg

image: prog_no_arg

clean:
	rm -rf prog_no_arg *.o 

.PHONY:
	no_arg arg all clean
