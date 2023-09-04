修改fs/fs.c中的sys_read，完成对标准输入的支持，修改lib/user/syscall.c，userprog/syscall-init.c，完成read系统调用
修改fs.c，实现sys_putchar
修改lib/kernel/print.S，实现cls_screen，修改lib/user/syscall.c，userprog/syscall-init.c，完成clear和putchar
添加lib/user/assert.h和lib/user/assert.c，实现assert函数
添加shell/shell.c和shell/shell.h，实现shell