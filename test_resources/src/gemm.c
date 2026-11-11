#include "io.h"
#include "power.h"
#include "rvv_test.h"
#include <stdint.h>

/* 标量 golden 参考实现：三重嵌套循环，int64 累加避免中间溢出，
 * 测试向量经过设计保证最终结果可被 int32 精确表示 */
static void
gemm_ref_4x4(const int32_t a[4][4], const int32_t b[4][4], int32_t c[4][4]) {
    for (size_t i = 0; i < 4; ++i) {
        for (size_t j = 0; j < 4; ++j) {
            int64_t s = 0;
            for (size_t k = 0; k < 4; ++k) {
                s += (int64_t)a[i][k] * (int64_t)b[k][j];
            }
            c[i][j] = (int32_t)s;
        }
    }
}

static int
run_case(const char* name, const int32_t a[4][4], const int32_t b[4][4]) {
    int32_t got[4][4];
    int32_t expected[4][4];

    rvv_matrix_mul_4x4_i32(a, b, got);
    gemm_ref_4x4(a, b, expected);

    for (size_t i = 0; i < 4; ++i) {
        for (size_t j = 0; j < 4; ++j) {
            if (got[i][j] != expected[i][j]) {
                printf(
                    "%s mismatch at C[%ld][%ld]: got %ld expected %ld\n", name, (long)i,
                    (long)j, (long)got[i][j], (long)expected[i][j]);
                return 0;
            }
        }
    }
    printf("%s ok\n", name);
    return 1;
}

int main(void) {
    TEST_START(__BASE_FILE__);
    rvv_enable();

    /* 单位矩阵：A×I = A，验证加载/存储通路与下标映射 */
    static const int32_t identity[4][4] = {
        { 1, 0, 0, 0 }, { 0, 1, 0, 0 }, { 0, 0, 1, 0 }, { 0, 0, 0, 1 }
    };
    static const int32_t a_nat[4][4] = {
        { 1, 2, 3, 4 }, { 5, 6, 7, 8 }, { 9, 10, 11, 12 }, { 13, 14, 15, 16 }
    };
    if (!run_case("gemm_identity", a_nat, identity))
        fail();

    /* 全 1 矩阵：C 每个元素 = 4，验证 4 次 vwmacc 累加与收窄 */
    static const int32_t ones[4][4] = {
        { 1, 1, 1, 1 }, { 1, 1, 1, 1 }, { 1, 1, 1, 1 }, { 1, 1, 1, 1 }
    };
    if (!run_case("gemm_ones", ones, ones))
        fail();

    /* 负值矩阵：验证符号扩展（vwmacc 是有符号乘加） */
    static const int32_t a_neg[4][4] = {
        { -1, -2, 3, 4 }, { 5, -6, 7, -8 }, { -9, 10, -11, 12 }, { 13, -14, 15, -16 }
    };
    static const int32_t b_neg[4][4] = {
        { -4, 3, -2, 1 }, { 8, -7, 6, -5 }, { -12, 11, -10, 9 }, { 16, -15, 14, -13 }
    };
    if (!run_case("gemm_neg", a_neg, b_neg))
        fail();

    /* 大数值：单个元素 1<<28，乘积约 9.4e8，落在 int32 范围内但不小，
     * 验证 64 位累加 + vnclip 窄化的精度通路 */
    static const int32_t a_big[4][4] = {
        { 1 << 28, 0, 0, 0 }, { 0, 1 << 27, 0, 0 }, { 0, 0, -1 << 26, 0 }, { 0, 0, 0, 1 << 25 }
    };
    static const int32_t b_big[4][4] = {
        { 7, 0, 0, 0 }, { 0, 5, 0, 0 }, { 0, 0, -3, 0 }, { 0, 0, 0, -11 }
    };
    if (!run_case("gemm_big", a_big, b_big))
        fail();

    /* 随机 4×4：xorshift 伪随机，幅值受限（|x|<100），
     * 最大累加 4*100*100 = 40000 远离溢出边界 */
    static int32_t a_rnd[4][4];
    static int32_t b_rnd[4][4];
    uint32_t seed = 0xdeadbeef;
    for (size_t i = 0; i < 4; ++i) {
        for (size_t j = 0; j < 4; ++j) {
            seed ^= seed << 13;
            seed ^= seed >> 7;
            seed ^= seed << 17;
            a_rnd[i][j] = (int32_t)(seed % 199) - 99;
            seed ^= seed << 13;
            seed ^= seed >> 7;
            seed ^= seed << 17;
            b_rnd[i][j] = (int32_t)(seed % 199) - 99;
        }
    }
    if (!run_case("gemm_random", a_rnd, b_rnd))
        fail();

    /* 矩阵自乘（C = A×A）：换一组下标组合，验证 k 方向累加顺序无关性 */
    int32_t got2[4][4];
    int32_t tmp[4][4];
    rvv_matrix_mul_4x4_i32(a_rnd, a_rnd, got2);
    gemm_ref_4x4(a_rnd, a_rnd, tmp);
    for (size_t i = 0; i < 4; ++i) {
        for (size_t j = 0; j < 4; ++j) {
            if (got2[i][j] != tmp[i][j]) {
                printf("gemm_square mismatch\n");
                fail();
            }
        }
    }
    printf("gemm_square ok\n");

    printf("vector gemm ok\n");
    pass();
    return 0;
}
