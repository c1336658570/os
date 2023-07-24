#include "print.h"
#include "init.h"
#include "debug.h"
//int _start() {
int main(void) {
  put_str("I am kernel\n");
  init_all();
  ASSERT(1 == 2);
  //asm volatile("sti");  //开中断 
   while(1);
   
   return 0;
}