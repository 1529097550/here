#include "io.h"

// 裸机猜数字：1..100，二分也能赢；0 退出
// 裸机没有 rand()，用 CLINT mtime 做种子 + LCG 滚动，保证每局数字不同

#define LCG_A 6364136223846793005ULL
#define LCG_C 1442695040888963407ULL

void main(void) {
    volatile uint64_t *mtime = (void *)0x200BFF8;   // QEMU virt CLINT
    uint64_t seed = *mtime;

    printf("\x1b[2J\x1b[H");                        // ANSI 清屏 + 光标回原点
    printf("=== Guess Number 1..100 ===\n");
    printf("(input a number, 0 quits)\n\n");

    while (1) {                                     // 外层：一局
        seed = seed * LCG_A + LCG_C;                // LCG 更新种子
        int secret = (int)(seed % 100) + 1;         // 映射到 1..100
        int tries = 0;

        printf("I picked a number in [1,100].\n");
        while (1) {                                 // 内层：一次猜测
            printf("your guess: ");
            long long g = input_dec();              // 跳过非数字，读到数字串为止

            if (g == 0) {                           // 0 = 中途放弃
                printf("give up! secret = %d\n", secret);
                break;
            }
            if (g < 1 || g > 100) {
                printf("out of range!\n");
                continue;
            }

            tries++;
            if (g < secret) {
                printf("guess %3lld -> too small\n", g);
            } else if (g > secret) {
                printf("guess %3lld -> too big\n", g);
            } else {
                printf("guess %3lld -> CORRECT! tries = %d\n", g, tries);
                break;
            }
        }

        printf("play again? 1 = yes, 0 = quit: ");
        long long again = input_dec();
        if (again == 0) {
            printf("bye\n");
            PowerOff();                             // 写 test-finisher，qemu/HERE 自动退出
        }
        printf("\n");
    }
}
