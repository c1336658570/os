#include "print.h"
#include "init.h"
#include "debug.h"
#include "memory.h"
//int _start() {
int main(void) {
  put_str("I am kernel\n");
  init_all();
  void *addr = get_kernel_pages(3);
  put_str("\nget_kernel_page start vaddr is");
  put_int((uint32_t)addr);
  put_str("\n");
  //asm volatile("sti");  //开中断 
   while(1);
   
   return 0;
}