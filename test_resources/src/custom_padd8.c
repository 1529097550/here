#include "io.h"

// padd8: packed 8-bit SIMD add (custom-0 space)
// 编码: funct7=0000000 rs2 rs1 funct3=000 rd opcode=0001011
static inline long long padd8(long long a, long long b) {
    long long r;
    __asm__ volatile (
        ".insn r 0x0b, 0, 0, %0, %1, %2"   // .insn r opcode, funct3, funct7, rd, rs1, rs2
        : "=r"(r) : "r"(a), "r"(b)
    );
    return r;
}

void main(void) {
    printf("custom-instr padd8 test\n");

    long long a = 0x11223344LL, b = 0x55667788LL;
    long long r = padd8(a, b);
    printf("padd8(%llx, %llx) = %llx\n", a, b, r);
    if (r == 0x6688aaccLL) {
        printf("PADD8 PASS\n");
    } else {
        printf("PADD8 FAIL\n");
    }

    PowerOff();
}
