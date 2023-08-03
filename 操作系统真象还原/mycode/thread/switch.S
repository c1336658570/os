[bits 32]
section .text
global switch_to
switch_to:
  ;栈中此处是返回地址
  ;遵循ABI原则，保护好esi、edi、ebx、ebp寄存器
  push esi
  push edi
  push ebx
  push ebp
  mov eax, [esp + 20]     ;得到栈中的参数cur，cur = [esp+20]
  mov [eax], esp          ;保存栈顶指针esp，task_struct的self_kstack字段
                          ;self_kstack在task_struct中的偏移为0

;-------以上是备份当前线程的环境，下面是恢复下一个线程的环境---------

  mov eax, [esp+24]       ;得到栈中的参数next，next = [esp+24]
  mov esp, [eax]          ;pcb的第一个成员是self_kstack成员
  ;它用来记录0级栈顶指针，被换上cpu时用来恢复0级栈
  ;0级栈中保存了进程或线程所有信息，包括3级栈指针
  pop ebp
  pop ebx
  pop edi
  pop esi
  ret                     ;返回到上面switch_to下面的那句注释的返回地址，
                          ;未由中断进入，第一次执行时会返回到kernel_thread
;如果此时的next线程之前尚未执行过，马上开始的是第一次执行，此时栈顶的值是函数kernel_thread
;的地址，这是由thread_create函数设置的，执行ret指令后处理器将去执行函数kernel_thread。如果next
;之前已经执行过了，这次是再次将其调度到处理器的话，此时栈顶的值是由调用函数switch_to的主调函
;数schedule留下的，这会继续执行schedule后面的流程。而switch_to是schedule最后一句代码，因此执
;行流程马上回到schedule的调用者intr_timer_handler中。schedule同样也是intr_timer_handler中最后一句
;代码，因此会完成intr_timer_handler，回到kernel.S中的jmp intr_exit，从而恢复任务的全部寄存器映像，
;之后通过iretd指令退出中断，任务被完全彻底地恢复。