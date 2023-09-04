#include "shell.h"
#include "stdint.h"
#include "fs.h"
#include "file.h"
#include "syscall.h"
#include "stdio.h"
#include "global.h"
#include "assert.h"
#include "string.h"
#include "buildin_cmd.h"

#define MAX_ARG_NR 16     //加上命令名外,最多支持15个参数

//存储输入的命令
static char cmd_line[MAX_PATH_LEN] = {0};
char final_path[MAX_PATH_LEN] = {0};    //用于洗路径时的缓冲

//用来记录当前目录,是当前目录的缓存,每次执行cd命令时会更新此内容
char cwd_cache[MAX_PATH_LEN] = {0};

//输出提示符
void print_prompt(void) {
  printf("[cccmmf@cccmmf:%s]$ ", cwd_cache);
}

//从键盘缓冲区中最多读入count个字节到buf
//接受2个参数，缓冲区buf和读入的字符数，功能是从键盘缓冲区中最多读入count个字节到buf。
static void readline(char *buf, int32_t count) {
  assert(buf != NULL && count > 0);
  char *pos = buf;
  while (read(stdin_no, pos, 1) != -1 && (pos - buf) < count) { //在不出错情况下，直到找到回车符才返回
    switch (*pos) {
      //找到回车或换行符后认为键入的命令结束，直接返回
      case '\n':
      case '\r':
        *pos = 0;
        putchar('\n');
        return;
      case '\b':
        //if判断是阻止删除非本次输入的信息，如果没有代码“if(buf[0] != '\b')”的话，
        //按下的退格键会将命令提示符及之前的内容删除
        if (buf[0] != '\b') {     //阻止删除非本次输入的信息
          --pos;      //退回到缓冲区cmd_line中上一个字符
          putchar('\b');
        }
        break;
      case 'l' - 'a':
        //1 先将当前的字符'l'-'a'置为0
        *pos = 0;
        //2 再将屏幕清空
        clear();
        //3 打印提示符
        print_prompt();
        //4 将之前键入的内容再次打印
        printf("%s", buf);
        break;
      case 'u' - 'a':
        while (buf != pos) {
          putchar('\b');
          *(pos--) = 0;
        }
        break;
      default:
        putchar(*pos);
        pos++;
    }
  }
  printf("readline: can`t find enter_key in the cmd_line, max num of char is 128\n");
}

//分析字符串cmd_str中以token为分隔符的单词，将各单词的指针存入argv数组
//接受3个参数，用户键入的原始命令串cmd_str、参数字符串数组argv、分隔符token。
//功能是分析字符串cmd_str中以token为分隔符的单词，将解析出来的单词的指针存入argv数组。
static int32_t cmd_parse(char *cmd_str, char **argv, char token) {
  assert(cmd_str != NULL);
  int32_t arg_idx = 0;
  //清空数组argv
  while (arg_idx < MAX_ARG_NR) {
    argv[arg_idx] = NULL;
    arg_idx++;
  }
  char *next = cmd_str;
  int32_t argc = 0;
  //外层循环处理整个命令行
  while (*next) {
    //去除命令字或参数之间的空格
    while (*next == token) {
      next++;
    }
    //处理最后一个参数后接空格的情况，如"ls dir2 "
    if (*next == 0) {
      break;
    }
    argv[argc] = next;

    //内层循环处理命令行中的每个命令字及参数
    while (*next && *next != token) {   //在字符串结束前找单词分隔符
      next++;
    }
    //如果未结束（是token字符），使tocken变成0
    if (*next) {
      *next++ = 0;  //将token字符替换为字符串结束符0，作为一个单词的结束，并将字符指针next指向下一个字符
    }

    if (argc > MAX_ARG_NR) {
      return -1;
    }
    argc++;
  }
  return argc;
}

//执行命令
static void cmd_execute(uint32_t argc, char** argv) {
  if (!strcmp("ls", argv[0])) {
    buildin_ls(argc, argv);
  } else if (!strcmp("cd", argv[0])) {
    if (buildin_cd(argc, argv) != NULL) {
      memset(cwd_cache, 0, MAX_PATH_LEN);
      strcpy(cwd_cache, final_path);
    }
  } else if (!strcmp("pwd", argv[0])) {
    buildin_pwd(argc, argv);
  } else if (!strcmp("ps", argv[0])) {
    buildin_ps(argc, argv);
  } else if (!strcmp("clear", argv[0])) {
    buildin_clear(argc, argv);
  } else if (!strcmp("mkdir", argv[0])) {
    buildin_mkdir(argc, argv);
  } else if (!strcmp("rmdir", argv[0])) {
    buildin_rmdir(argc, argv);
  } else if (!strcmp("rm", argv[0])) {
    buildin_rm(argc, argv);
  } else if (!strcmp("help", argv[0])) {
    buildin_help(argc, argv);
  } else {      //如果是外部命令,需要从磁盘上加载
    int32_t pid = fork();
    if (pid) {  //父进程
      int32_t status;
      int32_t child_pid = wait(&status);  //此时子进程若没有执行exit,my_shell会被阻塞,不再响应键入的命令
      if (child_pid == -1) {   //按理说程序正确的话不会执行到这句,fork出的进程便是shell子进程
        panic("my_shell: no child\n");
      }
      printf("child_pid %d, it's status: %d\n", child_pid, status);
    } else {    //子进程
      make_clear_abs_path(argv[0], final_path); //获取可执行文件argv[0]的绝对路径到final_path中
      argv[0] = final_path; //将argv[0]重新指向final_path

      //先判断下文件是否存在
      struct stat file_stat;
      memset(&file_stat, 0, sizeof(struct stat)); 
      if (stat(argv[0], &file_stat) == -1) {  //判断可执行文件是否存在
        printf("my_shell: cannot access %s: No such file or directory\n", argv[0]);
        exit(-1);
      } else {
        execv(argv[0], argv);
      }
    }
  }
}

char* argv[MAX_ARG_NR]; //argv必须为全局变量，为了以后exec的程序可访问参数
int32_t argc = -1;

//shell
void my_shell(void) {
  cwd_cache[0] = '/';
  while (1) {
    print_prompt();
    memset(final_path, 0, MAX_PATH_LEN);
    memset(cmd_line, 0, MAX_PATH_LEN);
    readline(cmd_line, MAX_PATH_LEN);
    if (cmd_line[0] == 0) {   //若只键入了一个回车
      continue;
    }

    //针对管道的处理
    char* pipe_symbol = strchr(cmd_line, '|');  //寻找管道字符'|'
    if (pipe_symbol) {
      //支持多重管道操作,如cmd1|cmd2|..|cmdn,cmd1的标准输出和cmdn的标准输入需要单独处理
      //cmd1是第1个命令，没人为它提供输入，因此其输入不变，仍为标准输入，但其输出是要传给命令cmd2，
      //因此cmd1的标准输出不能指向屏幕了，必须要重定向到管道的环形缓冲区中，
      //命令cmd2的标准输入必须也重定向到管道的环形缓冲区才能够获得cmd1的输出
      //cmdn是最后一个命令，它要将结果打印到屏幕，因此其标准输出不用改变，依然为屏幕
      
      //除cmd1的标准输入和cmdn的标准输出不变外，其他命令的标准输入和输出都要重定向到管道

      //1 生成管道
      int32_t fd[2] = {-1};   //fd[0]用于输入,fd[1]用于输出
      pipe(fd);
      //将标准输出重定向到fd[1],使后面的输出信息重定向到内核环形缓冲区
      fd_redirect(1, fd[1]);

      //2 第一个命令
      char* each_cmd = cmd_line;
      pipe_symbol = strchr(each_cmd, '|');
      *pipe_symbol = 0;

      //执行第一个命令,命令的输出会写入环形缓冲区
      argc = -1;
      argc = cmd_parse(each_cmd, argv, ' ');
      cmd_execute(argc, argv);

      //跨过'|',处理下一个命令
      each_cmd = pipe_symbol + 1;

      //将标准输入重定向到fd[0],使之指向内核环形缓冲区
      fd_redirect(0, fd[0]);
      //3 中间的命令,命令的输入和输出都是指向环形缓冲区
      while ((pipe_symbol = strchr(each_cmd, '|'))) {
        *pipe_symbol = 0;
        argc = -1;
        argc = cmd_parse(each_cmd, argv, ' ');
        cmd_execute(argc, argv);
        each_cmd = pipe_symbol + 1;
      }

      //4 处理管道中最后一个命令
      //将标准输出恢复屏幕
      fd_redirect(1, 1);

      //执行最后一个命令
      argc = -1;
      argc = cmd_parse(each_cmd, argv, ' ');
      cmd_execute(argc, argv);

      //5  将标准输入恢复为键盘
      fd_redirect(0, 0);

      //6 关闭管道
      close(fd[0]);
      close(fd[1]);
    } else {    //一般无管道操作的命令
      argc = -1;
      argc = cmd_parse(cmd_line, argv, ' ');
      if (argc == -1) {
        printf("num of arguments exceed %d\n", MAX_ARG_NR);
        continue;
      }
      cmd_execute(argc, argv);
    }
  }
  panic("my_shell: should not be here");
}
