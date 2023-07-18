#通过此脚本查看elf文件内部信息，通过如下命令查看kernel.bin的信息
#./xxd.sh kernel/kernel.bin 0 300
#gcc -c -o kernel/main.o kernel/main.c -m32
#ld kernel/main.o -Ttext 0xc0001500 -e main -o kernel/kernel.bin -melf_i386
#usage: sh xxd.sh 文件 起始地址 长度 
xxd -u -a -g 1 -s $2 -l $3 $1 

# 这个命令使用了 xxd 工具来对指定的文件进行十六进制编码和解码。具体选项的含义如下：
# -u：使用大写字母表示十六进制数。
# -a：在十六进制和ASCII码之间显示输出。
# -g 1：指定每个字节的分组大小为1字节。
# -s $2：指定从偏移量 $2 开始读取文件。
# -l $3：指定读取 $3 个字节的数据。
# 因此，该命令的意思是：
# 从文件 $1 中偏移量为 $2 处开始读取 $3 个字节的数据，并将其显示为十六进制形式
# （每个字节使用一个字节进行分组），并在每个字节的右侧显示相应的ASCII字符。
# 输出结果中的十六进制数会使用大写字母。

