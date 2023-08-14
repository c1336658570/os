echo |awk "{printf(\"%$2\n\",$1)}"
#此脚本第1个参数是待转换的数字，一般进制都支持，第2个参数是输出的格式。
#用法和C语言的printf一样。
#例如：sh calculator.sh 0x02000000/1024/1024 f 回车。
#32.000000
#32.000000 是结果。用浮点数格式 f 来显示结果的好处是避免用整数时的取整，误以为整除了。
#./calculator.sh 0x20000000/1024/1024 d   
#512