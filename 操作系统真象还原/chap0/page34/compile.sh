#!/bin/bash
gcc -E main.c
gcc -o main.bin -g main.c
./main.bin
echo "====================================================="
gcc -o main.bin -g main.c -v
#ltrace -S ./mian.bin  #使用ltrace跟踪程序运行时调用的库函数  -S参数查看系统调用  
#或使用trace ./main.bin 查看系统调用的封装函数  用-e trace=write来限制只看write系统调用