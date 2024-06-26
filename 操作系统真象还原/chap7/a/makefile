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

out/kernel/print.o:lib/kernel/print.S
	mkdir -p $(dir $@)
	nasm -f elf -o out/kernel/print.o lib/kernel/print.S -I ./lib/kernel/include

out/kernel/kernel.o:kernel/kernel.S
	mkdir -p $(dir $@)
	nasm -f elf -o out/kernel/kernel.o kernel/kernel.S

kernel/kernel.i:kernel/kernel.S
	mkdir -p $(dir $@)
	nasm -E -o kernel/kernel.i kernel/kernel.S

out/kernel/main.o:kernel/main.c
	mkdir -p $(dir $@)
	gcc -m32 -I lib/kernel/ -I lib/ -I kernel/ -c -fno-builtin -o out/kernel/main.o kernel/main.c -fno-stack-protector

out/interrupt.o:kernel/interrupt.c
	gcc -m32 -I lib/kernel/ -I lib/ -I kernel/ -c -fno-builtin -o out/interrupt.o kernel/interrupt.c -fno-stack-protector

out/init.o:kernel/init.c
	gcc -m32 -I lib/kernel/ -I lib/ -I kernel/ -c -fno-builtin -o out/init.o kernel/init.c -fno-stack-protector

out/kernel/kernel.bin:out/kernel/main.o out/kernel/print.o out/init.o out/interrupt.o out/kernel/kernel.o
	ld -melf_i386  -Ttext 0xc0001500 -e main -o ./out/kernel/kernel.bin \
    out/kernel/main.o out/kernel/print.o out/init.o out/interrupt.o out/kernel/kernel.o

build:$(OBJS_BIN) out/kernel/kernel.bin

#定义了一个名为run的目标，依赖于OBJS_BIN，表示要运行程序。
run: ${OBJS_BIN}
	make image
	bochs -f bochsrc.disk
	
debug:
	@echo $(OBJS_BIN)
	@echo $(I_flags)


clean:
	rm -rf *.img  *.out *.lock *.bin out

.PHONY:image build run clean