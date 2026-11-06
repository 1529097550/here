#include "io.h"
#include "custom_ops.h"
// packed-simd 指令家族裸机测试: padd8/psub8/padd16/psub16 (custom-0 空间, opcode=0x0b)
// 仅 HERE 模拟器 (custom-instr feature) 可执行; QEMU TCG 不识别 custom-0 会触发 illegal instruction

void main(void) {
    printf("packed-simd family test\n");

    long long a = 0x11223344LL, b = 0x55667788LL;
    long long r = padd8(a, b);
    printf("padd8(%llx, %llx) = %llx\n", a, b, r);
    if (r == 0x6688aaccLL) {
        printf("PADD8 PASS\n");
    } else {
        printf("PADD8 FAIL\n");
    }

    r = psub8(0x11223344LL, 0x05060708LL);
    if (r == 0x0c1c2c3cLL) {
        printf("PSUB8 PASS\n");
    } else {
        printf("PSUB8 FAIL\n");
    }

    r = padd16(0x00010002LL, 0x00030004LL);
    if (r == 0x00040006LL) {
        printf("PADD16 PASS\n");
    } else {
        printf("PADD16 FAIL\n");
    }

    r = psub16(0x00020001LL, 0x00040003LL);
    if (r == 0xfffefffeLL) {
        printf("PSUB16 PASS\n");
    } else {
        printf("PSUB16 FAIL\n");
    }

    PowerOff();
}
