OBJS_BIN=$(patsubst ./boot/%.S,out/%.bin,$(shell find ./boot -name '*.S'))
include_paths = ./include ./boot/include

I_flags = $(include_paths:%=-I%)

#image: ${OBJS_BIN} 定义了一个名为image的目标，依赖于OBJS_BIN，
#表示要生成一个名为mbr.img的磁盘映像文件，并将mbr.bin写入该磁盘映像中。
image: ${OBJS_BIN}
#创建一个大小为30MB的全0磁盘映像文件。
	dd if=/dev/zero of=mbr.img bs=512 count=61440
#mbr.bin文件写入mbr.img磁盘映像文件的第一个扇区。
	dd if=out/mbr.bin of=mbr.img bs=512 count=1 conv=notrunc
	dd if=out/loader.bin of=mbr.img bs=512 seek=2 count=4 conv=notrunc
#@-表示忽略删除过程中出现的错误。
	@-rm -rf *.bin	

#表示将以.S为扩展名的汇编源文件编译为以.bin为扩展名的二进制文件。
out/%.bin:boot/%.S
	mkdir -p $(dir $@)
	nasm $^ -o $@  $(I_flags)

build:$(OBJS_BIN)

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