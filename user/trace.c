#include "kernel/param.h"
#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int
main(int argc, char *argv[])
{
  int i;
  char *nargv[MAXARG];

  // 第一个参数是跟踪位掩码，其后至少还要有一个待执行命令。
  if(argc < 3 || (argv[1][0] < '0' || argv[1][0] > '9')){
    fprintf(2, "Usage: %s mask command\n", argv[0]);
    exit(1);
  }

  // 先为当前进程设置跟踪掩码，exec 后该字段会随进程保留。
  if (trace(atoi(argv[1])) < 0) {
    fprintf(2, "%s: trace failed\n", argv[0]);
    exit(1);
  }
  
  // 去掉 trace 自己的程序名和掩码，把剩余参数重新组织成 exec 的 argv。
  for(i = 2; i < argc && i < MAXARG; i++){
    nargv[i-2] = argv[i];
  }
  // exec 成功后不会返回；返回只可能表示命令装载失败。
  exec(nargv[0], nargv);
  exit(0);
}
