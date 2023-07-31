#include "memory.h"
#include "bitmap.h"
#include "stdint.h"
#include "global.h"
#include "debug.h"
#include "print.h"
#include "string.h"

#define PG_SIZE 4096    //表示页的尺寸

//因为0xc009f000是内核主线程栈顶，0xc009e000是内核主线程的pcb。
//一个页框大小的位图可表示128MB内存，位图位置安排在地址0xc009a000，这样本系统最大支持4个页框的位图，即512MB
#define MEM_BITMAP_BASE 0xc009a000  //用以表示内存位图的基址，其值为0xc009a000

//0xc0000000是内核从虚拟地址3G起。0x100000意指跨过低端1MB内存，使虚拟地址在逻辑上连续
#define K_HEAP_START 0xc0100000   //用来表示内核所使用的堆空间起始虚拟地址

#define PDE_IDX(addr) ((addr & 0xffc00000) >> 22)
#define PTE_IDX(addr) ((addr &0x003ff000) >> 12)

//内存池结构，生成两个实例用于管理内核内存池和用户内存池
struct pool {
  struct bitmap pool_bitmap;    //本内存池用到的位图结构，用于管理物理内存
  uint32_t phy_addr_start;      //本内存池所管理物理内存的起始地址
  uint32_t pool_size;           //本内存池字节容量
};

struct pool kernel_pool, user_pool; //生成内核内存池和用户内存池
struct virtual_addr kernel_vaddr;   //此结构用来给内核分配虚拟地址

//pf是内存池的flag，pg_cnt是页数，在pf表示的虚拟内存池中申请pg_cnt个虚拟页，成功则返回虚拟页的起始地址，失败则返回NULL
static void *vaddr_get(enum pool_flags pf, uint32_t pg_cnt) {
  //用于存储分配的起始虚拟地址和位图扫描函数bitmap_scan的返回值
  int vaddr_start = 0, bit_idx_start = -1;
  uint32_t cnt = 0;
  if (pf == PF_KERNEL) {  //在内核虚拟地址池中申请地址
    bit_idx_start = bitmap_scan(&kernel_vaddr.vaddr_bitmap, pg_cnt);  //扫描内核虚拟地址池中的位图
    if (bit_idx_start == -1) {
      return NULL;
    }
    //根据申请的页数量，即pg_cnt的值，逐次调用bitmap_set函数将相应位置1
    while (cnt < pg_cnt) {
      bitmap_set(&kernel_vaddr.vaddr_bitmap, bit_idx_start + cnt++, 1);
    }
    vaddr_start = kernel_vaddr.vaddr_start + bit_idx_start * PG_SIZE; //将bit_idx_start转换为虚拟地址
  } else {
    //用户内存池，将来实现用户进程再补充
  }
  return (void *)vaddr_start;
}

//得到虚拟地址vaddr对应的pte指针
//注：指针的值也就是虚拟地址，故此函数实际返回的是能够访问vaddr所在pte的虚拟地址
uint32_t *pte_ptr(uint32_t vaddr) {
  //先访问到页表自己 + 再用页目录项pde（页目录内页表的索引）作为pte的索引访问到页表 + 再用pte的索引作为页内偏移
  uint32_t *pte = (uint32_t *)(0xffc00000 + ((vaddr & 0xffc00000) >> 10) + PTE_IDX(vaddr) * 4);
  return pte;
}

//得到虚拟地址vaddr对应的pde的指针
uint32_t *pde_ptr(uint32_t vaddr) {
  //0xfffff用来访问到页表本身所在的地址
  uint32_t *pde = (uint32_t *)((0xfffff000) + (PDE_IDX(vaddr) * 4));
  return pde;
}

//在m_pool指向的物理内存池中分配1个物理页，成功则返回页框的物理地址，失败则返回NULL
static void *palloc(struct pool *m_pool) {
  //扫描或设置位图要保证原子操作
  int bit_idx = bitmap_scan(&m_pool->pool_bitmap, 1); //找到一个物理页面
  if (bit_idx == -1) {
    return NULL;
  }
  bitmap_set(&m_pool->pool_bitmap, bit_idx, 1);      //将bit_idx位置1
  //page_phyaddr用于保存分配的物理页地址
  uint32_t page_phyaddr = ((bit_idx * PG_SIZE) + m_pool->phy_addr_start);
  return (void *)page_phyaddr;  //将物理页地址转换成void *后返回
}
//页表中添加虚拟地址_vaddr与物理地址_page_phyaddr的映射
static void page_table_add(void* _vaddr, void* _page_phyaddr) {
  uint32_t vaddr = (uint32_t)_vaddr, page_phyaddr = (uint32_t)_page_phyaddr;
  uint32_t* pde = pde_ptr(vaddr); //获取虚拟地址_vaddr所在的pde的虚拟地址
  uint32_t* pte = pte_ptr(vaddr); //获得_vaddr所在的pte虚拟地址

  //执行*pte,会访问到空的pde。所以确保pde创建完成后才能执行*pte,
  //否则会引发page_fault。因此在*pde为0时,*pte只能出现在下面else语句块中的*pde后面。
  //先在页目录内判断目录项的P位，若为1,则表示该表已存在
  if (*pde & 0x00000001) {	 // 页目录项和页表项的第0位为P,此处判断目录项是否存在
    ASSERT(!(*pte & 0x00000001));

    if (!(*pte & 0x00000001)) {   // 只要是创建页表,pte就应该不存在,多判断一下放心
      *pte = (page_phyaddr | PG_US_U | PG_RW_W | PG_P_1);    // US=1,RW=1,P=1
    } else {			    //应该不会执行到这，因为上面的ASSERT会先执行。
      PANIC("pte repeat");
      *pte = (page_phyaddr | PG_US_U | PG_RW_W | PG_P_1);      // US=1,RW=1,P=1
    }
  } else {			    // 页目录项不存在,所以要先创建页目录再创建页表项.
    //页表中用到的页框一律从内核空间分配
    uint32_t pde_phyaddr = (uint32_t)palloc(&kernel_pool);  //申请新的物理页并将地址保存在变量pde_phyaddr中

    *pde = (pde_phyaddr | PG_US_U | PG_RW_W | PG_P_1);  //将新物理页的物理地址pde_phyaddr和相关属性写入此pde中

    //分配到的物理页地址pde_phyaddr对应的物理内存清0,
    //避免里面的陈旧数据变成了页表项,从而让页表混乱.
    //访问到pde对应的物理地址,用pte取高20位便可.
    //因为pte是基于该pde对应的物理地址内再寻址,
    //把低12位置0便是该pde对应的物理页的起始
    memset((void*)((int)pte & 0xfffff000), 0, PG_SIZE); //对刚刚申请的物理页初始化为0
        
    ASSERT(!(*pte & 0x00000001));
    //为vaddr对应的pte赋值，也就是把物理页地址和属性写进去
    *pte = (page_phyaddr | PG_US_U | PG_RW_W | PG_P_1);      // US=1,RW=1,P=1
  }
}

//两个参数，一个是pf，用来指明内存池，另外一个是pg_cnt，用来指明页数
//分配pg_cnt个页空间,成功则返回起始虚拟地址,失败时返回NULL
//其实此函数是申请虚拟地址，然后为此虚拟地址分配物理地址，并在页表中建立好虚拟地址到物理地址的映射
//此函数干了三件事：
//（1）通过vaddr_get在虚拟内存池中申请虚拟地址。
//（2）通过palloc在物理内存池中申请物理页。
//（3）通过page_table_add将以上两步得到的虚拟地址和物理地址在页表中完成映射。
void* malloc_page(enum pool_flags pf, uint32_t pg_cnt) {
  //用来监督申请的内存页数pg_cnt是否超过了物理内存池的容量。内核和用户空间各约16MB空间，保守起见用15MB来限制
  //pg_cnt<15*1024*1024/4096 = 3840页
  ASSERT(pg_cnt > 0 && pg_cnt < 3840);
  //**********   malloc_page的原理是三个动作的合成:   ***********
  //1通过vaddr_get在虚拟内存池中申请虚拟地址
  //2通过palloc在物理内存池中申请物理页
  //3通过page_table_add将以上得到的虚拟地址和物理地址在页表中完成映射

  //申请虚拟地址，如果失败就返回NULL
  void* vaddr_start = vaddr_get(pf, pg_cnt);
  if (vaddr_start == NULL) {
    return NULL;
  }

  uint32_t vaddr = (uint32_t)vaddr_start, cnt = pg_cnt;
  struct pool* mem_pool = pf & PF_KERNEL ? &kernel_pool : &user_pool; //判断要用的内存池属于内核，还是用户

  //因为虚拟地址是连续的,但物理地址可以是不连续的,所以逐个做映射
  while (cnt-- > 0) { //循环为虚拟页分配物理页并在页表中建立映射关系。
    void* page_phyaddr = palloc(mem_pool);
    if (page_phyaddr == NULL) {  // 失败时要将曾经已申请的虚拟地址和物理页全部回滚，在将来完成内存回收时再补充
      return NULL;
    }
    page_table_add((void*)vaddr, page_phyaddr); // 在页表中做映射   将虚拟地址vaddr映射为物理地址page_phyaddr
    vaddr += PG_SIZE;		 // 下一个虚拟页    继续下一个循环的申请物理页和页表映射
  }
  return vaddr_start;   //将分配的起始虚拟地址返回
}

//接受一个参数申请的页数pg_cnt，从内核物理内存池中申请pg_cnt页内存,成功则返回其虚拟地址,失败则返回NULL
void* get_kernel_pages(uint32_t pg_cnt) {
  void* vaddr =  malloc_page(PF_KERNEL, pg_cnt);  //调用malloc_page，返回的虚拟地址保存在变量vaddr中。
  if (vaddr != NULL) {	   // 若分配的地址不为空,将页框清0后返回
    memset(vaddr, 0, pg_cnt * PG_SIZE); //通过memset将此页清0，由于虚拟地址是连续的，所以置0的字节数直接用pg_cnt乘以PG_SIZE。
  }
  return vaddr;
}

//初始化内存池
static void mem_pool_init(int32_t all_mem) {
  put_str("mem_pool_init start\n");
  //页表大小 = 1页的页目录表+第0和第768个页目录项指向同一个页表+第769～1022个页目录项共指向254个页表，共256个页框
  uint32_t page_table_size = PG_SIZE * 256; //记录页目录表和页表占用的字节大小，总大小等于页目录表大小+页表大小

  //0x100000为低端1MB内存
  uint32_t used_mem = page_table_size + 0x100000; //用来记录当前已使用的内存字节数  低端1MB+页目录表和页表占用的内存

  uint32_t free_mem = all_mem - used_mem;   //用来存储目前可用的内存字节数
  //1页为4KB，不管总内存是不是4k的倍数对于以页为单位的内存分配策略，不足1页的内存不用考虑了
  uint16_t all_free_pages = free_mem / PG_SIZE; //来保存可用内存字节数free_mem转换成的物理页数
  uint16_t kernel_free_pages = all_free_pages / 2;  //用来存储分配给内核的空闲物理页
  uint16_t user_free_pages = all_free_pages - kernel_free_pages;  //把分配给内核后剩余的空闲物理页作为用户内存池的空闲物理页数量。

  //Kernel BitMap的长度，位图中的一位表示一页，以字节为单位
  //为简化位图操作，余数不处理，坏处是这样做会丢内存。好处是不用做内存的越界检查，因为位图表示的内存少于实际物理内存
  //坏处是会丢(1～7页)*2的内存：内核物理内存池+用户物理内存池。
  uint32_t kbm_length = kernel_free_pages / 8;  //来记录位图的长度
  //User BitMap的长度
  uint32_t ubm_length = user_free_pages / 8;    //用户内存池位图的长度

  //Kernel Pool start，内核内存池的起始地址
  uint32_t kp_start = used_mem;   //用于记录内核物理内存池的起始地址
  //User Pool start，用户内存池的起始地址
  uint32_t up_start = kp_start + kernel_free_pages * PG_SIZE; //用于记录用户物理内存池的起始地址

  //用以上的两个起始物理地址初始化各自内存池的起始地址
  kernel_pool.phy_addr_start = kp_start;
  user_pool.phy_addr_start = up_start;

  //用各自内存池中的容量字节数（物理页数乘以PG_SIZE）初始化各自内存池的pool_size
  kernel_pool.pool_size = kernel_free_pages * PG_SIZE;
  user_pool.pool_size = user_free_pages * PG_SIZE;

  //用各自内存池的位图长度kbm_length和ubm_length初始化各自内存池的位图中的位图字节长度成员btmp_bytes_len
  kernel_pool.pool_bitmap.btmp_bytes_len = kbm_length;
  user_pool.pool_bitmap.btmp_bytes_len = ubm_length;

  //*********    内核内存池和用户内存池位图   ***********
  //位图是全局的数据，长度不固定。
  //全局或静态的数组需要在编译时知道其长度，
  //而我们需要根据总内存大小算出需要多少字节。
  //所以改为指定一块内存来生成位图.
  //内核使用的最高地址是0xc009f000,这是主线程的栈地址.(内核的大小预计为70K左右)
  //32M内存占用的位图是2k.内核内存池的位图先定在MEM_BITMAP_BASE(0xc009a000)处.
  kernel_pool.pool_bitmap.bits = (void *)MEM_BITMAP_BASE;
  //用户内存池的位图紧跟在内核内存池位图之后
  user_pool.pool_bitmap.bits = (void *)(MEM_BITMAP_BASE + kbm_length);

  /******************** 输出内存池信息 **********************/
  put_str("kernel_pool_bitmap_start:");
  put_int((int)kernel_pool.pool_bitmap.bits);
  put_str("\n");
  put_str("kernel_pool_bitmap_end:");
  put_int((int)kernel_pool.pool_bitmap.bits + kernel_pool.pool_bitmap.btmp_bytes_len);
  put_str("\n");
  put_str("kernel_pool_phy_addr_start:");
  put_int(kernel_pool.phy_addr_start);
  put_str("\n");
  put_str("kernel_pool_phy_addr_end:");
  put_int(kernel_pool.phy_addr_start + kernel_pool.pool_size);
  put_str("\n");
  put_str("user_pool_bitmap_start:");

  put_int((int)user_pool.pool_bitmap.bits);
  put_str("\n");
  put_str("user_pool_bitmap_end:");
  put_int((int)user_pool.pool_bitmap.bits + user_pool.pool_bitmap.btmp_bytes_len);
  put_str("\n");
  put_str("user_pool_phy_addr_start:");
  put_int(user_pool.phy_addr_start);
  put_str("\n");
  put_str("user_pool_phy_addr_end:");
  put_int(user_pool.phy_addr_start + user_pool.pool_size);
  put_str("\n");

  //将位图置为0
  bitmap_init(&kernel_pool.pool_bitmap);
  bitmap_init(&user_pool.pool_bitmap);

  //初始化内核虚拟地址的位图，按实际物理内存大小生成数组。
  //用于维护内核堆的虚拟地址，所以要和内核内存池大小一致
  kernel_vaddr.vaddr_bitmap.btmp_bytes_len = kbm_length;
  //位图的数组指向一块未使用的内存，目前定位在内核内存池和用户内存池之外
  kernel_vaddr.vaddr_bitmap.bits = (void *)(MEM_BITMAP_BASE + kbm_length + ubm_length);

  kernel_vaddr.vaddr_start = K_HEAP_START;  //虚拟内存池的起始地址
  bitmap_init(&kernel_vaddr.vaddr_bitmap);
  put_str("mem_pool_init done\n");
}

//内存管理部分初始化入口
void mem_init() {
  put_str("mem_init start\n");
  uint32_t mem_bytes_total = (*(uint32_t *)(0xb00));  //0xb00是位与loader.S的total_mem_bytes，其中保存了内存总容量
  mem_pool_init(mem_bytes_total);   //初始化内存池
  put_str("mem init done\n");
}