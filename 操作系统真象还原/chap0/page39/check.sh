#!/bin/bash 
gcc -c -o ascii.o ascii.c
echo "====================================================================================================="
./xxd.sh ascii.c 0 59
echo "====================================================================================================="
./xxd.sh ascii.o 0 1504 | more | grep 61  #查看编译后的'abc\n'的ascii，61,62,63,0A，发现'\n'被编译后编程一个字节
#./xxd.sh ascii.o 0 1504 | more
echo "====================================================================================================="
