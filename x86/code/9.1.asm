;使用sti和cli然后观察IF标志位
sti 
cli 
jmp near $

times 510-($-$$) db 0
db 0x55, 0xaa
