// 简化版 grep，只支持 ^、.、*、$ 四种正则运算符。

#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

char buf[1024];
int match(char*, char*);

void
grep(char *pattern, int fd)
{
  int n, m;
  char *p, *q;

  m = 0;
  // 每次保留上轮最后一个不完整行，再把新读取的数据拼到其后。
  while((n = read(fd, buf+m, sizeof(buf)-m-1)) > 0){
    m += n;
    buf[m] = '\0';
    p = buf;
    while((q = strchr(p, '\n')) != 0){
      *q = 0;
      if(match(pattern, p)){
        *q = '\n';
        write(1, p, q+1 - p);
      }
      p = q+1;
    }
    if(m > 0){
      m -= p - buf;
      memmove(buf, p, m);
    }
  }
}

int
main(int argc, char *argv[])
{
  int fd, i;
  char *pattern;

  if(argc <= 1){
    fprintf(2, "usage: grep pattern [file ...]\n");
    exit(1);
  }
  pattern = argv[1];

  if(argc <= 2){
    grep(pattern, 0);
    exit(0);
  }

  for(i = 2; i < argc; i++){
    if((fd = open(argv[i], 0)) < 0){
      printf("grep: cannot open %s\n", argv[i]);
      exit(1);
    }
    grep(pattern, fd);
    close(fd);
  }
  exit(0);
}

// 正则匹配器取自 Kernighan 与 Pike 的《The Practice of Programming》第 9 章。

int matchhere(char*, char*);
int matchstar(int, char*, char*);

int
match(char *re, char *text)
{
  if(re[0] == '^')
    return matchhere(re+1, text);
  do{  // 还要尝试字符串末尾的空串，以支持 $ 等零长度匹配
    if(matchhere(re, text))
      return 1;
  }while(*text++ != '\0');
  return 0;
}

// matchhere：只判断正则 re 能否从 text 的当前位置开始匹配。
int matchhere(char *re, char *text)
{
  if(re[0] == '\0')
    return 1;
  // 星号作用于前一个字符；把星号后的正则交给 matchstar 反复尝试。
  if(re[1] == '*')
    return matchstar(re[0], re+2, text);
  if(re[0] == '$' && re[1] == '\0')
    return *text == '\0';
  if(*text!='\0' && (re[0]=='.' || re[0]==*text))
    return matchhere(re+1, text+1);
  return 0;
}

// matchstar：尝试让 c* 消耗不同数量的字符，再匹配剩余正则 re。
int matchstar(int c, char *re, char *text)
{
  do{  // 先尝试零次，再逐步尝试一次或多次，体现 * 的“零个或多个”语义
    if(matchhere(re, text))
      return 1;
  }while(*text!='\0' && (*text++==c || c=='.'));
  return 0;
}

