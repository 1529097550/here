#include "io.h"
#include "custom_ops.h"
// packed-simd 指令家族综合裸机测试: padd8/psub8/padd16/psub16 (custom-0 空间, opcode=0x0b)
// Golden Model 自动化比对: 同一组输入分别跑硬件指令与纯软件参考模型，逐条比对
// 仅 HERE 模拟器 (custom-instr feature) 可执行; QEMU TCG 不识别 custom-0 会触发 illegal instruction

// 纯软件 C 语言参考模型 (Golden Model)：与硬件实现完全独立
// lane_bits: 泳道宽度 (8 或 16)；is_sub: 0=加 1=减；进位/借位不跨泳道
static uint32_t golden_packed(uint32_t a, uint32_t b, int lane_bits, int is_sub) {
    int lanes = 32 / lane_bits;
    uint32_t lane_mask = (1u << lane_bits) - 1;
    uint32_t result = 0;
    for (int i = 0; i < lanes; i++) {
        uint32_t shift = (uint32_t)lane_bits * i;
        uint32_t la = (a >> shift) & lane_mask;
        uint32_t lb = (b >> shift) & lane_mask;
        uint32_t r = is_sub ? (la - lb) & lane_mask : (la + lb) & lane_mask;
        result |= r << shift;
    }
    return result;
}

// 测试数据集：覆盖正常值 / 全泳道溢出 / 半溢出 / 零 / 全 1 / 借位回绕 / 随机模式
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
    {0x0001FFFF, 0x00000002},
    {0x8000FFFF, 0x7FFF0001},
};

// 一条指令的比对结果统计
static int op_total = 0;
static int op_passed = 0;

static void compare_op(const char* name,
                       uint32_t (*hw_fn)(uint32_t, uint32_t),
                       int lane_bits,
                       int is_sub,
                       uint32_t a,
                       uint32_t b) {
    uint32_t hw = hw_fn(a, b);
    uint32_t sw = golden_packed(a, b, lane_bits, is_sub);
    op_total++;
    if (hw == sw) {
        printf("[PASS] %s a=%llx b=%llx -> %llx\n",
               name, (unsigned long long)a, (unsigned long long)b, (unsigned long long)hw);
        op_passed++;
    } else {
        printf("[FAIL] %s a=%llx b=%llx -> HW=%llx SW=%llx\n",
               name, (unsigned long long)a, (unsigned long long)b,
               (unsigned long long)hw, (unsigned long long)sw);
    }
}

void main(void) {
    printf("=== Custom Instruction Family Golden Model Verification ===\n");

    int total = sizeof(test_cases) / sizeof(test_cases[0]);
    for (int i = 0; i < total; i++) {
        uint32_t a = test_cases[i].a;
        uint32_t b = test_cases[i].b;
        compare_op("padd8 ", padd8, 8, 0, a, b);
        compare_op("psub8 ", psub8, 8, 1, a, b);
        compare_op("padd16", padd16, 16, 0, a, b);
        compare_op("psub16", psub16, 16, 1, a, b);
    }

    if (op_passed == op_total) {
        printf("=== ALL %d COMPARISONS PASSED SUCCESSFULLY! ===\n", op_total);
    } else {
        printf("=== VERIFICATION FAILED! (%d/%d passed) ===\n", op_passed, op_total);
    }

    PowerOff();
}
