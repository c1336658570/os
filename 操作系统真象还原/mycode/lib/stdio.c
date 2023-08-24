#include "stdio.h"
#include "interrupt.h"
#include "global.h"
#include "string.h"
#include "syscall.h"
#include "print.h"

#define va_start(ap, v) ap = (va_list)&v    //把ap指向第一个固定参数v ，&v的类型实际是二级指针，因此在&v前用(va_list)强制转换为一级指针后再赋值给ap
#define va_arg(ap, t) *((t *)(ap += 4))     //ap指向下一个参数并返回其值
#define va_end(ap) ap = NULL                //清除ap

//将整型转换成字符（integer to ascii）
/*
第1个参数value是待转换的整数，第2个参数buf_ptr_addr是保存转换结果的缓冲区指针的地址，缓冲区指针本身
已经是指针了，这里说的是指针所在的地址，也是指针的指针，即二级指针，因此类型是char**。这里用
二级指针的原因是：在函数实现中要将转换后的字符写到缓冲区指针指向的缓冲区中的1个或多个位置，
这取决于进制转换后的数值的位数，比如十六进制0xd转换成十进制后变成数值13，13要被转换成字符
'1'和'3'，所以数值13变成字符后将占用缓冲区中两个字符位置，字符写到哪里是由缓冲区指针决定的，因
此每写一个字符到缓冲区后，要更新缓冲区指针的值以使其指向缓冲区中下一个可写入的位置，这种原地修
改指针的操作，最方便的是用其下一级指针类型来保存此指针的地址，故将一级指针的地址作为参数传给二
级指针buf_ptr_addr，这样便于原地修改一级指针。
*/
static void itoa(uint32_t value, char **buf_ptr_addr, uint8_t base) {
  uint32_t m = value % base;  //求模，最先掉下来的是最低位
  uint32_t i = value / base;  //取整
  if (i) {
    itoa(i, buf_ptr_addr, base);
  }
  if (m < 10) { // 如果余数是0～9
    *((*buf_ptr_addr)++) = m + '0';   //将数字0～9转换为字符'0'～'9'
  } else {    //否则余数是A～F
    *((*buf_ptr_addr)++) = m - 10 + 'A';  //将数字A～F转换为字符'A'～'F'
  }
}

//将参数ap按照格式format输出到字符串str，并返回替换后str长度
uint32_t vsprintf(char *str, const char *format, va_list ap) {
  char *buf_ptr = str;
  const char *index_ptr = format;
  char index_char = *index_ptr;
  int32_t arg_int;
  char *arg_str;
  while (index_char) {  //循环直到index_char为结束字符'\0'
    if (index_char != '%') {
      *buf_ptr++ = index_char;  //复制format中除'%'以外的字符到buf_ptr，也就是复制到str中
      index_char = *(++index_ptr);
      continue;
    }
    index_char = *(++index_ptr);  //得到%后面的字符
    switch(index_char) {
      case 's':
        arg_str = va_arg(ap, char *);
        strcpy(buf_ptr, arg_str);
        buf_ptr += strlen(arg_str);
        index_char = *(++index_ptr);
        break;
      case 'c':
        *buf_ptr++ = va_arg(ap, char);
        index_char = *(++index_ptr);
        break;
      case 'd':
        arg_int = va_arg(ap, int);
        //若是负数，将其转为正数后，在正数前面输出个负号'-'
        if (arg_int < 0) {
          arg_int = 0 - arg_int;
          *buf_ptr++ = '-';
        }
        itoa(arg_int, &buf_ptr, 10);
        index_char = *(++index_ptr);
        break;
      case 'x':
        arg_int = va_arg(ap, int);    //获取下一个整型参数，将结果存储到变量arg_int
        itoa(arg_int, &buf_ptr, 16);  //转16进制并存到buf_ptr中
        index_char = *(++index_ptr);    //跳过格式字符并更新index_char
        break;
    }
  }
  return strlen(str);
}

//同printf不同的地方就是字符串不是写到终端，而是写到buf中
uint32_t sprintf(char *buf, const char *format, ...) {
  va_list args;
  uint32_t retval;
  va_start(args, format);
  retval = vsprintf(buf, format, args);
  va_end(args);
  return retval;
}

//格式化输出字符串format
uint32_t printf(const char *format, ...) {
  va_list args;
  va_start(args, format);   //使args指向format
  char buf[1024] = {0};     //用于存储拼接后的字符串
  vsprintf(buf, format, args);
  va_end(args);
  return write(1, buf, strlen(buf));
}