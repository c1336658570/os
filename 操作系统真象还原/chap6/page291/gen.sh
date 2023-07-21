#!/bin/bash 
gcc -m32 reg6.c -o reg6.bin -g 
gcc -m32 reg7.c -o reg7.bin -g 
gcc -S -m32 reg7.c -o reg7.S -g 
gcc -m32 reg8.c -o reg8.bin -g 