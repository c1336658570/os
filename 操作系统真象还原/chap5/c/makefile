OBJS_BIN=$(patsubst ./boot/%.S,out/boot/%.bin,$(shell find ./boot -name '*.S'))
include_paths =  ./boot/include

I_flags = $(include_paths:%=-I%)

#image: ${OBJS_BIN} 定义了一个名为image的目标，依赖于OBJS_BIN，
#表示要生成一个名为mbr.img的磁盘映像文件，并将mbr.bin写入该磁盘映像中。
image: ${OBJS_BIN} out/kernel/kernel.bin
#创建一个大小为30MB的全0磁盘映像文件。
	dd if=/dev/zero of=mbr.img bs=512 count=61440
#mbr.bin文件写入mbr.img磁盘映像文件的第一个扇区。
	dd if=out/boot/mbr.bin of=mbr.img bs=512 count=1 conv=notrunc
	dd if=out/boot/loader.bin of=mbr.img bs=512 seek=2 count=4 conv=notrunc
	dd if=./out/kernel/kernel.bin of=./mbr.img bs=512 count=200 seek=9 conv=notrunc
#@-表示忽略删除过程中出现的错误。
	@-rm -rf *.bin	

#表示将以.S为扩展名的汇编源文件编译为以.bin为扩展名的二进制文件。
out/boot/%.bin:boot/%.S
	mkdir -p $(dir $@)
	nasm $^ -o $@  $(I_flags)

out/kernel/main.o:kernel/main.c
	mkdir -p out/kernel
	gcc -c -o out/kernel/main.o kernel/main.c -m32

out/kernel/kernel.bin:out/kernel/main.o
	ld out/kernel/main.o  -Ttext 0xc0001500 -e main -o out/kernel/kernel.bin -melf_i386

build:$(OBJS_BIN) out/kernel/kernel.bin

#定义了一个名为run的目标，依赖于OBJS_BIN，表示要运行程序。
run: ${OBJS_BIN}
	make image
	bochs -f bochsrc.disk
#nasm -I ./boot/include/ -o ./out/boot/mbr.bin ./boot/mbr.S && dd if=./out/boot/mbr.bin of=./hd3M.img bs=512 count=1  conv=notrunc
#nasm -I ./boot/include/ -o ./out/boot/loader.bin ./boot/loader.S && dd if=./out/boot/loader.bin of=./hd3M.img bs=512 count=4 seek=2 conv=notrunc
#gcc -c -o out/kernel/main.o kernel/main.c
#/usr/bin/ld out/kernel/main.o -Ttext 0xc0001500 -e main -o out/kernel/kernel.bin && \
dd if=./out/kernel/kernel.bin of=./hd3M.img bs=512 count=200 seek=9 conv=notrunc
debug:
	@echo $(OBJS_BIN)
	@echo $(I_flags)


clean:
	rm -rf *.img  *.out *.lock *.bin out

.PHONY:image build run clean