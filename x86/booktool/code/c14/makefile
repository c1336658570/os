OBJS_BIN=c13_mbr.bin c14_core.bin c13.bin diskdata.txt

.PHONY:
#image: ${OBJS_BIN} 定义了一个名为image的目标，依赖于OBJS_BIN，
#表示要生成一个名为c05.img的磁盘映像文件，并将c05.bin写入该磁盘映像中。
image: ${OBJS_BIN}
#创建一个大小为120MB的全0磁盘映像文件。245760*512字节
	dd if=/dev/zero of=c13_mbr.img bs=512 count=245760
#c05.bin文件写入c05.img磁盘映像文件的第一个扇区。
	dd if=c13_mbr.bin of=c13_mbr.img bs=512 count=1 conv=notrunc
	dd if=c14_core.bin of=c13_mbr.img seek=1 bs=512 count=48 conv=notrunc
	dd if=c14_core.bin of=c13_mbr.img seek=50 bs=512 count=30 conv=notrunc
	dd if=diskdata.txt of=c13_mbr.img seek=100 bs=512 count=5 conv=notrunc
#@-表示忽略删除过程中出现的错误。
	@-rm -rf *.bin	

#表示将以.asm为扩展名的汇编源文件编译为以.bin为扩展名的二进制文件。
%.bin:%.asm
	nasm $^ -o $@ 

#定义了一个名为run的目标，依赖于OBJS_BIN，表示要运行程序。
run: ${OBJS_BIN}
	make image
	bochs -f bochsrc.disk


clean:
	rm -rf *.img  *.out *.lock *.bin 