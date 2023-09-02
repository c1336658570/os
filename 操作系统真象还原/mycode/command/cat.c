#include "syscall.h"
#include "stdio.h"
#include "string.h"

int main(int argc, char** argv) {
  //cat只支持1个参数，就是待查看的文件名
  if (argc > 2 || argc == 1) {
    printf("cat: only support 1 argument.\neg: cat filename\n");
    exit(-2);
  }
  int buf_size = 1024;
  char abs_path[512] = {0}; //abs_path用于存储参数的绝对路径
  void* buf = malloc(buf_size); //申请了1024字节的内存用作缓冲区buf
  if (buf == NULL) { 
    printf("cat: malloc memory failed\n");
    return -1;
  }
  //处理参数文件的路径为绝对路径，之后存入到abs_buf中
  if (argv[1][0] != '/') {  //不是以/路径作为起始，也就意味着是相对路径，需要修改为绝对路径
    getcwd(abs_path, 512);
    strcat(abs_path, "/");
    strcat(abs_path, argv[1]);
  } else {
    strcpy(abs_path, argv[1]);
  }
  int fd = open(abs_path, O_RDONLY);
  if (fd == -1) { 
    printf("cat: open: open %s failed\n", argv[1]);
    return -1;
  }
  int read_bytes= 0;
  while (1) {
    read_bytes = read(fd, buf, buf_size);
    if (read_bytes == -1) {
        break;
    }
    write(1, buf, read_bytes);
  }
  //释放buf并关闭参数文件
  free(buf);
  close(fd);
  return 66;
}
