// user/sleep.c
#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int main(int argc, char **argv) {
    if(argc < 2) {
        printf("usage: sleep <ticks>\n");
        exit(1);
    }
    // 参数单位是 xv6 的时钟滴答数而不是秒；先把命令行字符串转换为整数。
    sleep(atoi(argv[1]));
    exit(0);
}