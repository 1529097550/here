#include "io.h"
#include "power.h"
#include "rvv_test.h"
#include <stddef.h>
#include <stdint.h>

/* 标量 golden 参考实现：用 int64 累加避免中间值溢出，
 * 测试向量经过设计保证最终结果可被 int32 精确表示 */
static int32_t dot_ref_i32(const int32_t* a, const int32_t* b, size_t n) {
    int64_t s = 0;
    for (size_t i = 0; i < n; ++i) {
        s += (int64_t)a[i] * (int64_t)b[i];
    }
    return (int32_t)s;
}

static int
run_case(const char* name, const int32_t* a, const int32_t* b, size_t n) {
    int32_t got = vector_dot_product_int32(a, b, n);
    int32_t expected = dot_ref_i32(a, b, n);
    if (got != expected) {
        printf(
            "%s mismatch (n=%ld): got %ld expected %ld\n", name, (long)n, (long)got,
            (long)expected);
        return 0;
    }
    printf("%s (n=%ld) = %ld ok\n", name, (long)n, (long)got);
    return 1;
}

int main(void) {
    TEST_START(__BASE_FILE__);
    rvv_enable();

    /* 模拟器 VLEN=128bit，e32/m1 配置下 VLMAX = 128/32 = 4 lane */
    printf("VLMAX(e32, m1) = %ld\n", (long)rvv_vsetvl_e32m1((size_t)-1));

    /* 边界：n=0，循环体一次都不执行，结果必须为 0 */
    static const int32_t empty1[1] = { 1 };
    static const int32_t empty2[1] = { 2 };
    if (!run_case("dot_empty", empty1, empty2, 0))
        fail();

    /* 边界：单元素 */
    static const int32_t a1[1] = { 5 };
    static const int32_t b1[1] = { 7 };
    if (!run_case("dot_single", a1, b1, 1))
        fail();

    /* 尾部块：n=3 不足一个完整 VLMAX(4)，验证 tu/mu 尾部策略下
     * 累加向量残留 lane 不会污染结果 */
    static const int32_t a3[3] = { 1, 2, 3 };
    static const int32_t b3[3] = { 4, 5, 6 };
    if (!run_case("dot_tail3", a3, b3, 3))
        fail();

    /* 恰好一个完整块：n=4 == VLMAX */
    static const int32_t a4[4] = { 1, 2, 3, 4 };
    static const int32_t b4[4] = { 5, 6, 7, 8 };
    if (!run_case("dot_exact4", a4, b4, 4))
        fail();

    /* 多块：n=13 = 3 个完整块 + 1 个尾部元素，验证跨块累加 */
    static int32_t a13[13];
    static int32_t b13[13];
    for (size_t i = 0; i < 13; ++i) {
        a13[i] = (int32_t)i - 7;
        b13[i] = (int32_t)(i * 3) - 17;
    }
    if (!run_case("dot_multi13", a13, b13, 13))
        fail();

    /* 大数值 + 正负混合：单 lane 乘积接近 INT32_MAX，
     * 验证符号扩展与 32 位环绕语义与标量参考一致 */
    static const int32_t a_big[4] = { 1 << 28, -(1 << 27), 6, -9 };
    static const int32_t b_big[4] = { 5, -3, 7, 11 };
    if (!run_case("dot_big", a_big, b_big, 4))
        fail();

    /* 全负值 */
    static const int32_t a_neg[7] = { -1, -2, -3, -4, -5, -6, -7 };
    static const int32_t b_neg[7] = { -8, -9, -10, -11, -12, -13, -14 };
    if (!run_case("dot_neg", a_neg, b_neg, 7))
        fail();

    /* 随机长向量：n=100，充分覆盖多块迭代与尾块处理 */
    static int32_t a_rnd[100];
    static int32_t b_rnd[100];
    uint32_t seed = 0x12345678;
    for (size_t i = 0; i < 100; ++i) {
        /* 简单 xorshift 伪随机，限制幅值防止 64 位累加结果溢出 int32 */
        seed ^= seed << 13;
        seed ^= seed >> 7;
        seed ^= seed << 17;
        a_rnd[i] = (int32_t)(seed % 97) - 48;
        seed ^= seed << 13;
        seed ^= seed >> 7;
        seed ^= seed << 17;
        b_rnd[i] = (int32_t)(seed % 89) - 44;
    }
    if (!run_case("dot_rnd100", a_rnd, b_rnd, 100))
        fail();

    printf("vector dot product ok\n");
    pass();
    return 0;
}
