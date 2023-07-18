此程序用来演示三种检测内存的方法
此程序并为打印内存容量，而是通过直接查看变量total_mem_bytes的内容，查看内存容量。
变量 total_mem_bytes 的地址是 0xb00，所以咱们在 bochs 控制台中用 xp 指令来查看该地址就行了。
先make run跑起来，然后c，之后使用ctrl+c，然后通过xp 0xb00查看变量total_mem_bytes
查看完total_mem_bytes的内容后，可以使用calculator.sh将其中的16进制以字节为单位的内存大小转为以MB为单位的十进制形式
./calculator.sh 0x02000000/1024/1024 d