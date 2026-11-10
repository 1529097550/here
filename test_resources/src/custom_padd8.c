#include "io.h"
#include "custom_ops.h"
// packed-simd 指令家族裸机测试: padd8/psub8/padd16/psub16 (custom-0 空间, opcode=0x0b)
// 包含 Golden Model（纯软件参考模型）自动化比对: 硬件指令结果 vs 软件模型结果
// 仅 HERE 模拟器 (custom-instr feature) 可执行; QEMU TCG 不识别 custom-0 会触发 illegal instruction

// 纯软件 C 语言参考模型 (Golden Model)
static uint32_t golden_padd8(uint32_t a, uint32_t b) {
    uint8_t a0 = a & 0xFF, a1 = (a >> 8) & 0xFF, a2 = (a >> 16) & 0xFF, a3 = (a >> 24) & 0xFF;
    uint8_t b0 = b & 0xFF, b1 = (b >> 8) & 0xFF, b2 = (b >> 16) & 0xFF, b3 = (b >> 24) & 0xFF;

    uint8_t r0 = (uint8_t)(a0 + b0);
    uint8_t r1 = (uint8_t)(a1 + b1);
    uint8_t r2 = (uint8_t)(a2 + b2);
    uint8_t r3 = (uint8_t)(a3 + b3);

    return ((uint32_t)r0) |
           (((uint32_t)r1) << 8) |
           (((uint32_t)r2) << 16) |
           (((uint32_t)r3) << 24);
}

// 测试数据集：覆盖正常值 / 全泳道溢出 / 半溢出 / 零 / 全 1 / 随机模式
typedef struct {
    uint32_t a;
    uint32_t b;
} TestCase;

static const TestCase test_cases[] = {
    {0x01020304, 0x05060708},
    {0xFF01FE02, 0x01FF02FE},
    {0x80808080, 0x80808080},
    {0x00000000, 0x00000000},
    {0xFFFFFFFF, 0xFFFFFFFF},
    {0x12345678, 0x87654321},
};

void main(void) {
    printf("=== Starting Custom Instruction Backend Verification ===\n");

    int total = sizeof(test_cases) / sizeof(test_cases[0]);
    int passed = 0;

    for (int i = 0; i < total; i++) {
        uint32_t a = test_cases[i].a;
        uint32_t b = test_cases[i].b;

        // 硬件指令：经 .insn 内嵌汇编封装
        uint32_t hw_result = padd8(a, b);
        // 软件 Golden Model
        uint32_t sw_result = golden_padd8(a, b);

        if (hw_result == sw_result) {
            printf("[PASS] Test %d: a=%llx, b=%llx -> res=%llx\n",
                   i, (unsigned long long)a, (unsigned long long)b, (unsigned long long)hw_result);
            passed++;
        } else {
            printf("[FAIL] Test %d: a=%llx, b=%llx -> HW=%llx, SW=%llx\n",
                   i, (unsigned long long)a, (unsigned long long)b,
                   (unsigned long long)hw_result, (unsigned long long)sw_result);
        }
    }

    if (passed == total) {
        printf("=== ALL %d TESTS PASSED SUCCESSFULLY! ===\n", total);
    } else {
        printf("=== VERIFICATION FAILED! (%d/%d passed) ===\n", passed, total);
    }

    // 家族其余指令回归（ch2-end 的结果保持不变）
    long long a = 0x11223344LL, b = 0x55667788LL;
    long long r = padd8(a, b);
    if (r == 0x6688aaccLL) printf("PADD8 PASS\n"); else printf("PADD8 FAIL\n");
    r = psub8(0x11223344LL, 0x05060708LL);
    if (r == 0x0c1c2c3cLL) printf("PSUB8 PASS\n"); else printf("PSUB8 FAIL\n");
    r = padd16(0x00010002LL, 0x00030004LL);
    if (r == 0x00040006LL) printf("PADD16 PASS\n"); else printf("PADD16 FAIL\n");
    r = psub16(0x00020001LL, 0x00040003LL);
    if (r == 0xfffefffeLL) printf("PSUB16 PASS\n"); else printf("PSUB16 FAIL\n");

    PowerOff();
}
