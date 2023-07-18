启动内存分页机制
运行后，使用info gdt查看gdt基址的变化
使用info tab 查看页表
处理器提供了指令 invlpg（invalidate page），它用于在 TLB 中刷新某个虚拟地址对应的条目，处理器是用虚拟地址来检索 TLB 的，因此很自然地，指令 invlpg 的操作数也是虚拟地址，其指令格式为 invlpg m,其中 m 表示操作数为虚拟内存地址，并不是立即数，比如要更新虚拟地址 0x1234 对应的条目，指令为 invlpg [0x1234]，并不是 invlpg 0x1234。