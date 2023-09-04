#include "pipe.h"
#include "file.h"
#include "fs.h"
#include "ioqueue.h"
#include "memory.h"
#include "thread.h"

//判断文件描述符local_fd是否是管道
//接受1个参数，文件描述符local_fd，也就是pcb中数组fd_table的下标，功能是判断文件描述符local_fd是否是管道
bool is_pipe(uint32_t local_fd) {
  uint32_t global_fd = fd_local2global(local_fd);
  return file_table[global_fd].fd_flag == PIPE_FLAG;
}

//创建管道,成功返回0,失败返回-1
//接受1个参数，存储管道文件描述符的数组pipefd，功能是创建管道，
//成功后描述符pipefd[0]可用于读取管道，pipefd[1]可用于写入管道，然后返回值为0，否则返回−1
int32_t sys_pipe(int32_t pipefd[2]) {
  int32_t global_fd = get_free_slot_in_global();  //获得可用的文件结构空位下标

  //申请一页内核内存做环形缓冲区
  file_table[global_fd].fd_inode = get_kernel_pages(1); //分配一页内核内存做管道的环形缓冲区

  //初始化环形缓冲区
  ioqueue_init((struct ioqueue*)file_table[global_fd].fd_inode);
  if (file_table[global_fd].fd_inode == NULL) {
    return -1;
  }

  //将fd_flag复用为管道标志
  file_table[global_fd].fd_flag = PIPE_FLAG;

  //将fd_pos复用为管道打开数
  file_table[global_fd].fd_pos = 2;   //表示有两个文件描述符对应这个管道
  pipefd[0] = pcb_fd_install(global_fd);
  pipefd[1] = pcb_fd_install(global_fd);

  return 0;
}

//从管道中读数据
//接受3个参数，文件描述符fd、存储数据的缓冲区buf、读取数据的数量count，功能是从文件描述符fd中读取count字节到buf
uint32_t pipe_read(int32_t fd, void* buf, uint32_t count) {
  char* buffer = buf;
  uint32_t bytes_read = 0;
  uint32_t global_fd = fd_local2global(fd);

  //获取管道的环形缓冲区
  struct ioqueue* ioq = (struct ioqueue*)file_table[global_fd].fd_inode;

  //选择较小的数据读取量,避免阻塞
  uint32_t ioq_len = ioq_length(ioq);
  uint32_t size = ioq_len > count ? count : ioq_len;
  while (bytes_read < size) {
    *buffer = ioq_getchar(ioq);
    bytes_read++;
    buffer++;
  }
  return bytes_read;
}

//往管道中写数据
uint32_t pipe_write(int32_t fd, const void* buf, uint32_t count) {
  uint32_t bytes_write = 0;
  uint32_t global_fd = fd_local2global(fd);
  struct ioqueue* ioq = (struct ioqueue*)file_table[global_fd].fd_inode;

  //选择较小的数据写入量,避免阻塞
  uint32_t ioq_left = bufsize - ioq_length(ioq);
  uint32_t size = ioq_left > count ? count : ioq_left;

  const char* buffer = buf;
  while (bytes_write < size) {
    ioq_putchar(ioq, *buffer);
    bytes_write++;
    buffer++;
  }
  return bytes_write;
}
