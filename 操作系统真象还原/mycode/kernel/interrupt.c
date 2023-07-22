#include "interrupt.h"
#include "stdint.h"
#include "global.h"
#include "io.h"
#include "print.h"

#define IDT_DESC_CNT 0X21     //目前总共支持的中断数

#define PIC_M_CTRL 0x20   //主片的控制端口是0x20
#define PIC_M_DATA 0x21   //主片的数据端口是0x21
#define PIC_S_CTRL 0xa0   //从片的控制端口是0xa0
#define PIC_S_DATA 0xa1   //从片的数据端口是0xa1

//中断门描述符结构体
struct gate_desc {
  uint16_t    func_offset_low_word;   //中断处理程序在目标段内的偏移量15～0位
  uint16_t    selector;       //中断处理程序目标代码段描述符选择子
  uint8_t     dcount;   //此项为双字计数字段，是门描述符中的第4字节。此项固定值，不用考虑
  uint8_t     attribute;  //3-0是TYPE（D110），4是S（0），6-5是DPL，7是P
  uint16_t    func_offset_high_word;  //中断处理程序在目标段内的偏移量31～16位
};

//静态函数声明，非必须
static void make_idt_desc(struct gate_desc  *p_gdesc, uint8_t attr, intr_handler function);
//IDT属于全局数据结构，所以声明为static类型
static struct gate_desc idt[IDT_DESC_CNT];    //idt是中断描述符表，一共0x21(33)个元素

extern intr_handler intr_entry_table[IDT_DESC_CNT];    //定义在kernel.S中的中断处理函数入口

//初始化可编程中断控制器8259A
static void pic_init(void) {
  //初始化主片
  //第0位是IC4位，表示是否需要指定ICW4，我们需要在ICW4中设置EOI
  //为手动方式，所以需要ICW4。由于我们要级联从片，所以将ICW1中的第1位SNGL置为0，表示级联。
  //设置第3位的LTIM为0，表示边沿触发。ICW1中第4位是固定为1。其他位不设置。
  outb(PIC_M_CTRL, 0X11);   //ICW1：边沿触发，级联8259,需要ICW4
  //中断向量号0～31已经被占用或保留，从32起才可用，所以我们往主片PIC_M_DATA端口（主片的数据端口0x21）
  //写入的ICW2值为0x20，即32。这说明主片的起始中断向量号为0x20，即IR0对应的中断向量号为0x20，
  //这是我们的时钟所接入的引脚。IR1～IR7对应的中断向量号依次往下排。
  outb(PIC_M_DATA, 0x20);   //ICW2：起始中断向量号为0x20  IR0-IR7 = 0X20~0x27
  //ICW3专用于设置主从级联时用到的引脚，第2位置为1代表IR2连接从片。
  outb(PIC_M_DATA, 0x04);   //ICW3：IR2接从片
  //要设置其中的第0位：μPM位，它设置当前处理器的类型，咱们所在的开发平台是x86，所以要将其置为1。
  //此外还要设置ICW4的第1位：EOI的工作模式位。如果为1，8259A会自动结束中断，
  //这里我们需要手动向8259A发送中断，所以将此位设置为0。
  //EOI是End OF Interrupt，就是告诉8259A中
  //断处理程序执行完了，8259A现在可以接受下一个中断信号啦。
  outb(PIC_M_DATA, 0x01);   //ICW4：8086模式，正常EOI（手动EOI模式）

  //初始化从片 
  outb(PIC_S_CTRL, 0X11);   //ICW1：边沿触发，级联8259,需要ICW4
  outb(PIC_S_DATA, 0x28);   //ICW2：起始中断向量号为0x28  IR0-IR7 = 0X28~0x2f
  //向从片发送ICW3，ICW3专用于设置级联的引脚设置主片的时候是设置用
  //IR2引脚来级联从片，所以此处要告诉从片连接到主片的IR2上，即ICW2值为0x02
  outb(PIC_S_DATA, 0x02);   //ICW3：设置从片连接到主片IR2引脚
  outb(PIC_S_DATA, 0x01);   //ICW4：8086模式，正常EOI

  //打开主片IR0，就是目前只支持时钟中断
  //设置中断屏蔽寄存器 IMR，只放行主片上 IR0 的时钟中断，屏蔽其他外部设备的中断。
  outb(PIC_M_DATA, 0xfe);   //主片OCW1为0xfe，不屏蔽IR0（时钟中断），其他都屏蔽
  outb(PIC_S_DATA, 0XFF);   //从片OCW1为0xff，全屏蔽

  put_str("pic_init done\n");
}

//创建中断描述符
//三个参数：中断门描述符的指针、中断描述符内的属性及中断描述符内对应的中断处理函数
//原理是将后两个参数写入第1个参数所指向的中断门描述符中，实际上就是用后面的两个参数构建第1个参数指向的中断门描述符。
static void make_idt_desc(struct gate_desc *p_gdesc, uint8_t attr, intr_handler function) {
  p_gdesc->func_offset_low_word = 0x0000FFFF & (uint32_t)function;
  p_gdesc->selector = SELECTOR_K_CODE;  //指向内核代码段的选择子
  p_gdesc->dcount = 0;
  p_gdesc->attribute = attr;
  p_gdesc->func_offset_high_word = (0xFFFF0000 & (uint32_t)function) >> 16;
}

//初始化中断描述符表
static void idt_desc_init(void) {
  int i = 0;
  for (; i < IDT_DESC_CNT; ++i) {
    /*
    第1个参数便是中断描述符表idt的数组成员指针，第2个参数IDT_DESC_ATTR_DPL0是描述符的属性
    第3个参数是在kernel.S中定义的中断描述符地址数组intr_entry_table中的元素值，即中断处理程序的地址
    */
    make_idt_desc((idt + i), IDT_DESC_ATTR_DPL0, intr_entry_table[i]);
  }
  put_str("idt_desc_init done\n");
}

//完成有关中断初始化工作
void idt_init(void) {
  put_str("idt_init start\n");
  idt_desc_init();
  pic_init();

  //加载idt
  //sizeof(idt) – 1得到idt的段界限limit
  //将idt的地址挪到高32位
  uint64_t idt_operand = ((sizeof(idt) - 1) | ((uint64_t)((uint32_t)idt) << 16));
  //内存约束是传递的C变量的指针给汇编指令当作操作数，也就是说，在“lidt %0”中，
  //%0其实是idt_operand的地址&idt_operand，并不是idt_operand的值
  asm volatile ("lidt %0" : : "m"(idt_operand));
  put_str("idt_init done\n");
}