all: 12

%.o:%.asm
	@nasm -f elf 1.asm -o 1.o
	@nasm -f elf 2.asm -o 2.o

12:1.o 2.o
	@ld -melf_i386 1.o 2.o -o 12

build:1.o 2.o 12

run:12
	@./12

readelf:12
	readelf -e ./12

clean:
	@rm -rf *.o 12

.PHONY:clean