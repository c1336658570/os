本程序用来演示内联汇编
内联汇编规则
满足这个格式 asm [volatile] ("assembly code")，关键字 volatile 是可选项，告诉 gcc：“不要修改我写的汇编代码，请原样保留”。volatile 和__volatile__是一样的，是由 gcc 定义的宏：#define __volatile__ volatile。assembly code 甚至可以为空。
assembly code 的规则：
（1）指令必须用双引号引起来，无论双引号中是一条指令或多条指令。
（2）一对双引号不能跨行，如果跨行需要在结尾用反斜杠'\'转义。
（3）指令之间用分号'；'、换行符'\n'或换行符加制表符'\n''\t'分隔。