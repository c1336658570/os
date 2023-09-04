BUILD_DIR = ./build
DISK_IMG = os.img
ENTRY_POINT = 0xc0001500
AS = nasm
CC = gcc
LD = ld
LIB = -I lib/ -I lib/kernel/ -I lib/user/ -I kernel/ -I device/ -I thread/ -I userprog/ -I fs/ -I shell/
ASFLAGS = -f elf -g
ASBINLIB = -I boot/include/
#-fno-builtin告诉编译器不要采用内部函数
#-Wstrict-prototypes选项要求函数声明中必须有参数类型，否则编译时发出警告
#-Wmissing-prototypes选项要求函数必须有声明，否则编译时发出警告。
CFLAGS = -m32 -Wall $(LIB) -c -fno-builtin -W -Wstrict-prototypes \
         -Wmissing-prototypes -fno-stack-protector -ggdb -nostdinc -mno-sse -g
LDFLAGS = -melf_i386 -Ttext $(ENTRY_POINT) -e main -Map $(BUILD_DIR)/kernel.map

#bochs参数
BOCHS_PATH=/usr/local/bin/bochs
BOCHS_GDB_PATH=/home/cccmmf/bochs-gdb/bin/bochs
BOCHS_PORT="target remote 127.0.0.1:1234"
BOCHS_GDB_FLAG='gdbstub:enabled=1,port=1234,text_base=0,data_base=0,bss_base=0'

OBJS = $(BUILD_DIR)/main.o $(BUILD_DIR)/init.o $(BUILD_DIR)/interrupt.o \
	   $(BUILD_DIR)/timer.o $(BUILD_DIR)/kernel.o $(BUILD_DIR)/print.o \
	   $(BUILD_DIR)/debug.o $(BUILD_DIR)/memory.o $(BUILD_DIR)/bitmap.o \
	   $(BUILD_DIR)/string.o $(BUILD_DIR)/thread.o $(BUILD_DIR)/list.o \
	   $(BUILD_DIR)/switch.o  $(BUILD_DIR)/console.o $(BUILD_DIR)/sync.o \
	   $(BUILD_DIR)/keyboard.o  $(BUILD_DIR)/ioqueue.o $(BUILD_DIR)/tss.o \
	   $(BUILD_DIR)/process.o  $(BUILD_DIR)/syscall.o $(BUILD_DIR)/syscall-init.o \
	   $(BUILD_DIR)/stdio.o $(BUILD_DIR)/ide.o $(BUILD_DIR)/stdio-kernel.o $(BUILD_DIR)/fs.o	\
	   $(BUILD_DIR)/inode.o $(BUILD_DIR)/file.o $(BUILD_DIR)/dir.o $(BUILD_DIR)/fork.o	\
		 $(BUILD_DIR)/shell.o $(BUILD_DIR)/assert.o $(BUILD_DIR)/buildin_cmd.o \
		 $(BUILD_DIR)/exec.o $(BUILD_DIR)/wait_exit.o $(BUILD_DIR)/pipe.o

##############     MBR代码编译     ############### 
$(BUILD_DIR)/mbr.bin: boot/mbr.S 
	$(AS) $(ASBINLIB) $< -o $@

##############     bootloader代码编译     ###############
$(BUILD_DIR)/loader.bin: boot/loader.S 
	$(AS) $(ASBINLIB) $< -o $@

##############     c代码编译     ###############
$(BUILD_DIR)/main.o: kernel/main.c lib/kernel/print.h \
        lib/stdint.h kernel/init.h thread/thread.h 
	$(CC) $(CFLAGS) $< -o $@

$(BUILD_DIR)/init.o: kernel/init.c kernel/init.h lib/kernel/print.h \
        lib/stdint.h kernel/interrupt.h device/timer.h
	$(CC) $(CFLAGS) $< -o $@

$(BUILD_DIR)/interrupt.o: kernel/interrupt.c kernel/interrupt.h \
        lib/stdint.h kernel/global.h lib/kernel/io.h lib/kernel/print.h
	$(CC) $(CFLAGS) $< -o $@

$(BUILD_DIR)/timer.o: device/timer.c device/timer.h lib/stdint.h\
         lib/kernel/io.h lib/kernel/print.h
	$(CC) $(CFLAGS) $< -o $@

$(BUILD_DIR)/debug.o: kernel/debug.c kernel/debug.h \
        lib/kernel/print.h lib/stdint.h kernel/interrupt.h
	$(CC) $(CFLAGS) $< -o $@

$(BUILD_DIR)/string.o: lib/string.c lib/string.h \
        lib/stdint.h  kernel/debug.h  lib/string.h kernel/global.h
	$(CC) $(CFLAGS) $< -o $@

$(BUILD_DIR)/bitmap.o: lib/kernel/bitmap.c lib/kernel/bitmap.h \
        lib/kernel/print.h lib/stdint.h kernel/interrupt.h \
		kernel/debug.h  lib/string.h kernel/global.h
	$(CC) $(CFLAGS) $< -o $@

#$(BUILD_DIR)/memory.o: kernel/memory.c kernel/memory.h \
#        lib/kernel/bitmap.h lib/stdint.h 
#	$(CC) $(CFLAGS) $< -o $@

$(BUILD_DIR)/memory.o: kernel/memory.c kernel/memory.h lib/stdint.h lib/kernel/bitmap.h \
	   	kernel/global.h kernel/global.h kernel/debug.h lib/kernel/print.h \
		lib/kernel/io.h kernel/interrupt.h lib/string.h lib/stdint.h
			$(CC) $(CFLAGS) $< -o $@

$(BUILD_DIR)/thread.o: thread/thread.c thread/thread.h lib/stdint.h \
	    kernel/global.h lib/kernel/bitmap.h kernel/memory.h lib/string.h \
		lib/stdint.h lib/kernel/print.h kernel/interrupt.h kernel/debug.h
		$(CC) $(CFLAGS) $< -o $@

$(BUILD_DIR)/list.o: lib/kernel/list.c lib/kernel/list.h kernel/global.h lib/stdint.h \
	        kernel/interrupt.h
		$(CC) $(CFLAGS) $< -o $@

$(BUILD_DIR)/console.o: device/console.c device/console.h lib/stdint.h \
	    lib/kernel/print.h thread/sync.h lib/kernel/list.h kernel/global.h \
		thread/thread.h thread/thread.h
		$(CC) $(CFLAGS) $< -o $@

$(BUILD_DIR)/sync.o: thread/sync.c thread/sync.h lib/kernel/list.h kernel/global.h \
	    lib/stdint.h thread/thread.h lib/string.h lib/stdint.h kernel/debug.h \
		kernel/interrupt.h
		$(CC) $(CFLAGS) $< -o $@
$(BUILD_DIR)/keyboard.o: device/keyboard.c device/keyboard.h lib/kernel/print.h \
	    lib/stdint.h kernel/interrupt.h lib/kernel/io.h thread/thread.h \
		lib/kernel/list.h kernel/global.h thread/sync.h thread/thread.h
		$(CC) $(CFLAGS) $< -o $@
$(BUILD_DIR)/ioqueue.o: device/ioqueue.c device/ioqueue.h lib/stdint.h thread/thread.h \
	lib/kernel/list.h kernel/global.h thread/sync.h thread/thread.h kernel/interrupt.h \
	kernel/debug.h
	$(CC) $(CFLAGS) $< -o $@
$(BUILD_DIR)/tss.o: userprog/tss.c userprog/tss.h thread/thread.h lib/stdint.h \
	lib/kernel/list.h kernel/global.h lib/string.h lib/stdint.h \
	lib/kernel/print.h
	$(CC) $(CFLAGS) $< -o $@

$(BUILD_DIR)/process.o: userprog/process.c userprog/process.h thread/thread.h \
	lib/stdint.h lib/kernel/list.h kernel/global.h kernel/debug.h \
	kernel/memory.h lib/kernel/bitmap.h userprog/tss.h kernel/interrupt.h \
	lib/string.h lib/stdint.h
	$(CC) $(CFLAGS) $< -o $@

$(BUILD_DIR)/syscall.o: lib/user/syscall.c lib/user/syscall.h lib/stdint.h
	$(CC) $(CFLAGS) $< -o $@

$(BUILD_DIR)/syscall-init.o: userprog/syscall-init.c userprog/syscall-init.h \
	lib/stdint.h lib/user/syscall.h lib/kernel/print.h thread/thread.h \
	lib/kernel/list.h kernel/global.h lib/kernel/bitmap.h kernel/memory.h
	$(CC) $(CFLAGS) $< -o $@

$(BUILD_DIR)/stdio.o: lib/stdio.c lib/stdio.h lib/stdint.h kernel/interrupt.h \
	lib/stdint.h kernel/global.h lib/string.h lib/user/syscall.h lib/kernel/print.h
	$(CC) $(CFLAGS) $< -o $@

$(BUILD_DIR)/ide.o: device/ide.c device/ide.h lib/stdint.h thread/sync.h \
	lib/kernel/list.h kernel/global.h thread/thread.h lib/kernel/bitmap.h \
	kernel/memory.h lib/kernel/io.h lib/stdio.h lib/stdint.h lib/kernel/stdio-kernel.h\
	kernel/interrupt.h kernel/debug.h device/console.h device/timer.h lib/string.h
	$(CC) $(CFLAGS) $< -o $@

$(BUILD_DIR)/stdio-kernel.o: lib/kernel/stdio-kernel.c lib/kernel/stdio-kernel.h lib/stdint.h \
	lib/kernel/print.h lib/stdio.h lib/stdint.h device/console.h kernel/global.h
	$(CC) $(CFLAGS) $< -o $@

$(BUILD_DIR)/fs.o: fs/fs.c fs/fs.h lib/stdint.h device/ide.h thread/sync.h lib/kernel/list.h \
	kernel/global.h thread/thread.h lib/kernel/bitmap.h kernel/memory.h fs/super_block.h \
	fs/inode.h fs/dir.h lib/kernel/stdio-kernel.h lib/string.h lib/stdint.h kernel/debug.h \
	kernel/interrupt.h lib/kernel/print.h
	$(CC) $(CFLAGS) $< -o $@
$(BUILD_DIR)/inode.o: fs/inode.c fs/inode.h lib/stdint.h lib/kernel/list.h \
	kernel/global.h fs/fs.h device/ide.h thread/sync.h thread/thread.h \
	lib/kernel/bitmap.h kernel/memory.h fs/file.h kernel/debug.h \
	kernel/interrupt.h lib/kernel/stdio-kernel.h
	$(CC) $(CFLAGS) $< -o $@

$(BUILD_DIR)/file.o: fs/file.c fs/file.h lib/stdint.h device/ide.h thread/sync.h \
	lib/kernel/list.h kernel/global.h thread/thread.h lib/kernel/bitmap.h \
	kernel/memory.h fs/fs.h fs/inode.h fs/dir.h lib/kernel/stdio-kernel.h \
	kernel/debug.h kernel/interrupt.h
	$(CC) $(CFLAGS) $< -o $@

$(BUILD_DIR)/dir.o: fs/dir.c fs/dir.h lib/stdint.h fs/inode.h lib/kernel/list.h \
	kernel/global.h device/ide.h thread/sync.h thread/thread.h \
	lib/kernel/bitmap.h kernel/memory.h fs/fs.h fs/file.h \
	lib/kernel/stdio-kernel.h kernel/debug.h kernel/interrupt.h
	$(CC) $(CFLAGS) $< -o $@
$(BUILD_DIR)/fork.o: userprog/fork.c userprog/fork.h thread/thread.h lib/stdint.h \
	lib/kernel/list.h kernel/global.h lib/kernel/bitmap.h kernel/memory.h \
	userprog/process.h kernel/interrupt.h kernel/debug.h \
	lib/kernel/stdio-kernel.h
	$(CC) $(CFLAGS) $< -o $@
$(BUILD_DIR)/shell.o: shell/shell.c shell/shell.h lib/stdint.h fs/fs.h \
	lib/user/syscall.h lib/stdio.h lib/stdint.h kernel/global.h lib/user/assert.h
	$(CC) $(CFLAGS) $< -o $@

$(BUILD_DIR)/assert.o: lib/user/assert.c lib/user/assert.h lib/stdio.h lib/stdint.h
	$(CC) $(CFLAGS) $< -o $@

$(BUILD_DIR)/buildin_cmd.o: shell/buildin_cmd.c shell/buildin_cmd.h lib/stdint.h \
	lib/user/syscall.h lib/stdio.h lib/stdint.h lib/string.h fs/fs.h
	$(CC) $(CFLAGS) $< -o $@

$(BUILD_DIR)/exec.o: userprog/exec.c userprog/exec.h thread/thread.h lib/stdint.h \
	lib/kernel/list.h kernel/global.h lib/kernel/bitmap.h kernel/memory.h \
	lib/kernel/stdio-kernel.h fs/fs.h lib/string.h lib/stdint.h
	$(CC) $(CFLAGS) $< -o $@

$(BUILD_DIR)/wait_exit.o: userprog/wait_exit.c userprog/wait_exit.h \
	userprog/../thread/thread.h lib/stdint.h lib/kernel/list.h \
	kernel/global.h lib/kernel/bitmap.h kernel/memory.h kernel/debug.h \
	thread/thread.h lib/kernel/stdio-kernel.h
	$(CC) $(CFLAGS) $< -o $@

$(BUILD_DIR)/pipe.o: shell/pipe.c shell/pipe.h lib/stdint.h kernel/memory.h \
	lib/kernel/bitmap.h kernel/global.h lib/kernel/list.h fs/fs.h fs/file.h \
	device/ide.h thread/sync.h thread/thread.h fs/dir.h fs/inode.h fs/fs.h \
	device/ioqueue.h thread/thread.h
	$(CC) $(CFLAGS) $< -o $@

##############    汇编代码编译    ###############
$(BUILD_DIR)/kernel.o: kernel/kernel.S
	$(AS) $(ASFLAGS) $< -o $@
$(BUILD_DIR)/print.o: lib/kernel/print.S
	$(AS) $(ASFLAGS) -I lib/kernel/include $< -o $@
$(BUILD_DIR)/switch.o: thread/switch.S
		$(AS) $(ASFLAGS) $< -o $@

##############    链接所有目标文件    #############
$(BUILD_DIR)/kernel.bin: $(OBJS)
	$(LD) $(LDFLAGS) $^ -o $@

mk_dir:
	mkdir -p $(BUILD_DIR)

image: mk_dir $(BUILD_DIR)/kernel.bin $(BUILD_DIR)/mbr.bin $(BUILD_DIR)/loader.bin
	dd if=/dev/zero of=os.img bs=512 count=409248
	dd if=$(BUILD_DIR)/mbr.bin of=os.img bs=512 count=1  conv=notrunc
	dd if=$(BUILD_DIR)/loader.bin of=os.img bs=512 count=4 seek=2 conv=notrunc
	dd if=$(BUILD_DIR)/kernel.bin of=os.img bs=512 count=200 seek=9 conv=notrunc
	dd if=command/prog_no_arg of=/home/cccmmf/操作系统/操作系统真象还原/mycode/os.img bs=512 count=100 seek=220 conv=notrunc
	dd if=command/prog_arg of=/home/cccmmf/操作系统/操作系统真象还原/mycode/os.img bs=512 count=100 seek=300 conv=notrunc
	dd if=command/cat of=/home/cccmmf/操作系统/操作系统真象还原/mycode/os.img bs=512 count=100 seek=400 conv=notrunc
	dd if=command/cat.c of=/home/cccmmf/操作系统/操作系统真象还原/mycode/os.img bs=512 count=100 seek=500 conv=notrunc
	dd if=command/prog_pipe of=/home/cccmmf/操作系统/操作系统真象还原/mycode/os.img bs=512 count=100 seek=600 conv=notrunc


clean:
	rm -rf $(BUILD_DIR) && rm -rf $(DISK_IMG) bochs.out os.img.lock

build: mk_dir $(BUILD_DIR)/kernel.bin $(BUILD_DIR)/mbr.bin $(BUILD_DIR)/loader.bin

run: all
	${BOCHS_PATH} -qf bochsrc.disk

run-gdb:all
	${BOCHS_GDB_PATH} -qf bochsrc-gdb.disk  ${BOCHS_GDB_FLAG} &
	gdb ./build/kernel.bin -ex ${BOCHS_PORT}
	pkill bochs
	make clean

gdb_symbol:
	objcopy --only-keep-debug $(BUILD_DIR)/kernel.bin $(BUILD_DIR)/kernel.sym

all: mk_dir image gdb_symbol

.PHONY : clean all image run build