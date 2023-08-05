#include "keyboard.h"
#include "print.h"
#include "interrupt.h"
#include "io.h"
#include "global.h"

#define KBD_BUF_PORT 0x60 //键盘buffer寄存器端口号0x60

//键盘中断处理程序
//每收到一个中断，就通过put_char('k')打印字符'k'，然后再调用inb(KBD_BUF_PORT)读取8042的输出缓冲区寄存器。
static void intr_keyboard_handler(void) {
  //必须要读取输出缓冲区寄存器，否则8042不再继续响应键盘中断
  //inb是有返回值的，它返回的是从端口读取的数据根据ABI约定，
  //返回值是存放在寄存器eax中的，我们这里没有将返回值赋给内存变量
  uint8_t scancode = inb(KBD_BUF_PORT);
  put_int(scancode);
  return;
}

//键盘初始化
void keyboard_init() {
  put_str("keyboard init start\n");
  register_handler(0x21, intr_keyboard_handler);
  put_str("keyboard init done\n");
}