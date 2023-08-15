#include "interrupt.h"
#include "stdint.h"
#include "global.h"
#include "io.h"
#include "print.h"

#define IDT_DESC_CNT 0X81     //目前总共支持的中断数

#define PIC_M_CTRL 0x20   //主片的控制端口是0x20
#define PIC_M_DATA 0x21   //主片的数据端口是0x21
#define PIC_S_CTRL 0xa0   //从片的控制端口是0xa0
#define PIC_S_DATA 0xa1   //从片的数据端口是0xa1

#define EFLAGS_IF 0X00000200    //eflags寄存器中的IF位为1
#define GET_EFLAGS(EFLAG_VAR) asm volatile("pushfl; popl %0" : "=g"(EFLAG_VAR))

extern uint32_t syscall_handler(void);

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

char *intr_name[IDT_DESC_CNT];    //保存异常的名字
intr_handler idt_table[IDT_DESC_CNT]; //定义中断处理程序数组，保存中断处理程序的入口

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

  //主片IR0是时钟中断，IR1是键盘中断
  //IRQ2用于级联从片，必须打开，否则无法响应从片上的中断。
  //主片上打开的中断有IRQ0的时钟，IRQ1的键盘和级联从片的IRQ2，其他全部关闭
  outb(PIC_M_DATA, 0xf8);   //主片OCW1为0xfc，不屏蔽IR2、IR1（键盘中断）和IR0（时钟中断），其他都屏蔽
  //硬盘上有两个ata通道，也称为IDE通道。第1个ata通道上的两个硬盘（主和从）的中断信号挂在8259A从片的IRQ14上
  //第2个ata通道接在8259A从片的IRQ15
  //打开从片上的IRQ14，此引脚接收硬盘控制器的中断
  outb(PIC_S_DATA, 0xbf);   //从片OCW1为0xff，全屏蔽

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
  int i, lastindex = IDT_DESC_CNT - 1;
  for (i = 0; i < IDT_DESC_CNT; ++i) {
    /*
    第1个参数便是中断描述符表idt的数组成员指针，第2个参数IDT_DESC_ATTR_DPL0是描述符的属性
    第3个参数是在kernel.S中定义的中断描述符地址数组intr_entry_table中的元素值，即中断处理程序的地址
    */
    make_idt_desc((idt + i), IDT_DESC_ATTR_DPL0, intr_entry_table[i]);
  }
  //单独处理系统调用，系统调用对应的中断门dpl为3，中断处理程序为单独的syscall_handler
  //若指定为0级，则在3级环境下执行int指令会产生GP异常。
  make_idt_desc(&idt[lastindex], IDT_DESC_ATTR_DPL3, syscall_handler);
  put_str("idt_desc_init done\n");
}

//通用中断处理函数，一般用在异常出现时的处理
//以后各设备都会注册自己的中断处理程序，不会再使用general_intr_handler。我们没有为各种异常注
//册相应的中断处理程序，这里是用general_intr_handler作为通用的中断处理程序来“假装处理”异常的
static void general_intr_handler(uint8_t vec_nr) {
  if (vec_nr == 0x27 || vec_nr == 0x2f) {
    //IRQ7和IRQ15会产生伪中断（spurious interrupt），如中断线路上电气信号异常，
    //或是中断请求设备本身有问题,无需处理。0x2f是从片8259A上最后一个IRQ引脚，保留项
    //它们无法通过IMR寄存器屏蔽，所以在这里单独处理它们。
    return;
  }
  //将光标置为 0，从屏幕左上角清出一片打印异常信息的区域，方便阅读
  set_cursor(0);
  int cursor_pos = 0;
  while (cursor_pos < 320) {  //循环清空4行内容
    put_char(' ');
    cursor_pos++;
  }

  set_cursor(0);    //重置光标为屏幕左上角
  put_str("!!!!!!!   excetion message begin   !!!!!!!!\n");
  set_cursor(88);   //从第2行第8个字符开始打印
  put_str(intr_name[vec_nr]);
  if (vec_nr == 14) {   //若为Pagefault，将缺失的地址打印出来并悬停
    int page_fault_vaddr = 0;
    asm ("movl %%cr2, %0" : "=r" (page_fault_vaddr));   //cr2是存放造成page_fault的地址
    put_str("\npage fault addr is");
    put_int(page_fault_vaddr);
  }
  put_str("\n!!!!!!!   excetion message end   !!!!!!!!\n");

  //能进入中断处理程序就表示已经处在关中断情况下
  //不会出现调度进程的情况。故下面的死循环不会再被中断
  //处理器进入中断后会自动把标志寄存器eflags中的IF位置0，即中断处理程序在关中断的情况下运行
  while (1);
}

//完成中断处理函数注册及异常名注册
static void exception_init(void) {
  int i;
  for (i = 0; i < IDT_DESC_CNT; ++i) {
    //idt_table数组中的函数是在进入中断后根据中断向量号调用的
    //在kernel/kernel.S的call [idt_table + %1*4]
    idt_table[i] = general_intr_handler;  //默认为general_intr_handler
    //由于intr_name是用来记录IDT_DESC_CNT（33）个的名称，但异常只有20个，
    //所以先一律赋值为“unknown”，这样就保证intr_name[20～32]不指空了
    intr_name[i] = "unknown"; //统一赋值unknown
  }
  intr_name[0] = "#DE Divide Error";
  intr_name[1] = "#DB Debug Exception";
  intr_name[2] = "NMI Interrupt";
  intr_name[3] = "#BP Breakpoint Exception";
  intr_name[4] = "#OF Overflow Exception";
  intr_name[5] = "#BR BOUND Range Exceeded Exception";
  intr_name[6] = "#UD Invalid Opcode Exception";
  intr_name[7] = "#NM Device Not Available Exception";
  intr_name[8] = "#DF Double Fault Exception";
  intr_name[9] = "Coprocessor Segment Overrun";
  intr_name[10] = "#TS Invalid TSS Exception";
  intr_name[11] = "#NP Segment Not Present";
  intr_name[12] = "#SS Stack Fault Exception";
  intr_name[13] = "#GP General Protection Exception";
  intr_name[14] = "#PF Page-Fault Exception";
  // intr_name[15] 第15项是intel保留项，未使用
  intr_name[16] = "#MF x87 FPU Floating-Point Error";
  intr_name[17] = "#AC Alignment Check Exception";
  intr_name[18] = "#MC Machine-Check Exception";
  intr_name[19] = "#XF SIMD Floating-Point Exception";
}

//开中断并返回之前中断状态
enum intr_status intr_enable(void) {
  enum intr_status old_status;
  if (INTR_ON == intr_get_status()) {
    old_status = INTR_ON;
    return old_status;
  } else {
    old_status = INTR_OFF;
    asm volatile ("sti");   //开中断
    return old_status;
  }
}

//关中断，并返回之前的中段状态
enum intr_status intr_disable(void) {
  enum intr_status old_status;
  if (INTR_ON ==intr_get_status()) {
    old_status = INTR_ON;
    asm volatile ("cli" : : : "memory"); //关中断
    return old_status;
  } else {
    old_status = INTR_OFF;
    return old_status;
  }
}

//将中断状态设置为status
enum intr_status intr_set_status(enum intr_status status) {
  return status & INTR_ON ? intr_enable() : intr_disable();
}

//获取当前中断状态
enum intr_status intr_get_status(void) {
  uint32_t eflags = 0;
  GET_EFLAGS(eflags); //获取elfags寄存器的值
  return (eflags & EFLAGS_IF) ? INTR_ON : INTR_OFF;
}

void register_handler(uint8_t vector_no, intr_handler function) {
  //idt_table数组中的函数是在进入中断后根据中断向量号调用的
  //见kernel/kernel.S的call[idt_table + %1*4]
  idt_table[vector_no] = function;
}

//完成有关中断初始化工作
void idt_init(void) {
  put_str("idt_init start\n");
  idt_desc_init();    //初始化中断描述符表
  exception_init();     //异常名初始化并注册通常的中断处理函数
  pic_init();     //初始化8259A

  //加载idt
  //sizeof(idt) – 1得到idt的段界限limit
  //将idt的地址挪到高32位
  uint64_t idt_operand = ((sizeof(idt) - 1) | ((uint64_t)((uint32_t)idt) << 16));
  //内存约束是传递的C变量的指针给汇编指令当作操作数，也就是说，在“lidt %0”中，
  //%0其实是idt_operand的地址&idt_operand，并不是idt_operand的值
  asm volatile ("lidt %0" : : "m"(idt_operand));
  put_str("idt_init done\n");
}