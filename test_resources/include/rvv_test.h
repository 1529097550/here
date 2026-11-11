#pragma once

#include <stddef.h>
#include <stdint.h>

static inline void rvv_enable(void) {
    uintptr_t mstatus;
    asm volatile("csrr %0, mstatus" : "=r"(mstatus));
    mstatus |= (uintptr_t)0x3 << 9;
    asm volatile("csrw mstatus, %0" : : "r"(mstatus) : "memory");
}

static inline size_t rvv_vsetvl_e8m1(size_t avl) {
    size_t vl;
    asm volatile("vsetvli %0, %1, e8, m1, tu, mu" : "=r"(vl) : "r"(avl));
    return vl;
}

static inline size_t rvv_vsetvl_e16m1(size_t avl) {
    size_t vl;
    asm volatile("vsetvli %0, %1, e16, m1, tu, mu" : "=r"(vl) : "r"(avl));
    return vl;
}

static inline size_t rvv_vsetvl_e32m1(size_t avl) {
    size_t vl;
    asm volatile("vsetvli %0, %1, e32, m1, tu, mu" : "=r"(vl) : "r"(avl));
    return vl;
}

static inline size_t rvv_vsetvl_e64m1(size_t avl) {
    size_t vl;
    asm volatile("vsetvli %0, %1, e64, m1, tu, mu" : "=r"(vl) : "r"(avl));
    return vl;
}

static inline void rvv_load_i32_v8(const int32_t* src) {
    asm volatile("vle32.v v8, (%0)" : : "r"(src) : "memory");
}

static inline void rvv_load_i32_v16(const int32_t* src) {
    asm volatile("vle32.v v16, (%0)" : : "r"(src) : "memory");
}

static inline void rvv_load_i32_v24(const int32_t* src) {
    asm volatile("vle32.v v24, (%0)" : : "r"(src) : "memory");
}

static inline void rvv_store_i32_v8(int32_t* dst) {
    asm volatile("vse32.v v8, (%0)" : : "r"(dst) : "memory");
}

static inline void rvv_store_i32_v24(int32_t* dst) {
    asm volatile("vse32.v v24, (%0)" : : "r"(dst) : "memory");
}

static inline void rvv_load_i16_v8(const int16_t* src) {
    asm volatile("vle16.v v8, (%0)" : : "r"(src) : "memory");
}

static inline void rvv_store_i16_v8(int16_t* dst) {
    asm volatile("vse16.v v8, (%0)" : : "r"(dst) : "memory");
}

static inline void rvv_load_i8_v8(const int8_t* src) {
    asm volatile("vle8.v v8, (%0)" : : "r"(src) : "memory");
}

static inline void rvv_store_i8_v8(int8_t* dst) {
    asm volatile("vse8.v v8, (%0)" : : "r"(dst) : "memory");
}

#define RVV_WRAP_VV_I32(name, asm_name)                                                  \
    static inline void rvv_##name##_vv_i32(                                              \
        int32_t* dst, const int32_t* lhs, const int32_t* rhs, size_t n) {                \
        while (n) {                                                                      \
            size_t vl = rvv_vsetvl_e32m1(n);                                             \
            rvv_load_i32_v16(lhs);                                                       \
            rvv_load_i32_v8(rhs);                                                        \
            asm volatile(asm_name " v24, v16, v8" ::: "memory");                         \
            rvv_store_i32_v24(dst);                                                      \
            lhs += vl;                                                                   \
            rhs += vl;                                                                   \
            dst += vl;                                                                   \
            n -= vl;                                                                     \
        }                                                                                \
    }

#define RVV_WRAP_VX_I32(name, asm_name)                                                  \
    static inline void rvv_##name##_vx_i32(                                              \
        int32_t* dst, const int32_t* src, int32_t x, size_t n) {                         \
        while (n) {                                                                      \
            size_t vl = rvv_vsetvl_e32m1(n);                                             \
            rvv_load_i32_v8(src);                                                        \
            asm volatile(asm_name " v24, v8, %0" : : "r"(x) : "memory");                 \
            rvv_store_i32_v24(dst);                                                      \
            src += vl;                                                                   \
            dst += vl;                                                                   \
            n -= vl;                                                                     \
        }                                                                                \
    }

#define RVV_WRAP_VI_I32(name, asm_name, imm)                                             \
    static inline void rvv_##name##_vi_i32(int32_t* dst, const int32_t* src, size_t n) { \
        while (n) {                                                                      \
            size_t vl = rvv_vsetvl_e32m1(n);                                             \
            rvv_load_i32_v8(src);                                                        \
            asm volatile(asm_name " v24, v8, " #imm ::: "memory");                       \
            rvv_store_i32_v24(dst);                                                      \
            src += vl;                                                                   \
            dst += vl;                                                                   \
            n -= vl;                                                                     \
        }                                                                                \
    }

RVV_WRAP_VV_I32(add, "vadd.vv")
RVV_WRAP_VV_I32(sub, "vsub.vv")
RVV_WRAP_VV_I32(mul, "vmul.vv")
RVV_WRAP_VV_I32(and, "vand.vv")
RVV_WRAP_VV_I32(or, "vor.vv")
RVV_WRAP_VV_I32(xor, "vxor.vv")
RVV_WRAP_VV_I32(sll, "vsll.vv")
RVV_WRAP_VV_I32(srl, "vsrl.vv")
RVV_WRAP_VV_I32(sra, "vsra.vv")
RVV_WRAP_VV_I32(min, "vmin.vv")
RVV_WRAP_VV_I32(max, "vmax.vv")
RVV_WRAP_VV_I32(minu, "vminu.vv")
RVV_WRAP_VV_I32(maxu, "vmaxu.vv")

RVV_WRAP_VX_I32(add, "vadd.vx")
RVV_WRAP_VX_I32(sub, "vsub.vx")
RVV_WRAP_VX_I32(rsub, "vrsub.vx")
RVV_WRAP_VX_I32(mul, "vmul.vx")
RVV_WRAP_VX_I32(and, "vand.vx")
RVV_WRAP_VX_I32(or, "vor.vx")
RVV_WRAP_VX_I32(xor, "vxor.vx")
RVV_WRAP_VX_I32(sll, "vsll.vx")
RVV_WRAP_VX_I32(srl, "vsrl.vx")
RVV_WRAP_VX_I32(sra, "vsra.vx")

RVV_WRAP_VI_I32(add3, "vadd.vi", 3)
RVV_WRAP_VI_I32(and7, "vand.vi", 7)
RVV_WRAP_VI_I32(or5, "vor.vi", 5)
RVV_WRAP_VI_I32(xor6, "vxor.vi", 6)
RVV_WRAP_VI_I32(sll1, "vsll.vi", 1)
RVV_WRAP_VI_I32(srl1, "vsrl.vi", 1)
RVV_WRAP_VI_I32(sra1, "vsra.vi", 1)

#define RVV_WRAP_MASK_VV_I32(name, asm_name)                                             \
    static inline void rvv_##name##_vv_i32(                                              \
        uint8_t* mask_out, const int32_t* lhs, const int32_t* rhs, size_t n) {           \
        while (n) {                                                                      \
            size_t vl = rvv_vsetvl_e32m1(n);                                             \
            rvv_load_i32_v16(lhs);                                                       \
            rvv_load_i32_v8(rhs);                                                        \
            asm volatile(asm_name " v0, v16, v8\n\tvsm.v v0, (%0)"                       \
                         :                                                               \
                         : "r"(mask_out)                                                 \
                         : "memory");                                                    \
            lhs += vl;                                                                   \
            rhs += vl;                                                                   \
            mask_out += (vl + 7) / 8;                                                    \
            n -= vl;                                                                     \
        }                                                                                \
    }

/* These helpers materialize mask registers with vsm.v. They are useful for
 * ISA coverage once mask load/store is available in the emulator. The runnable
 * smoke tests below use register-local mask producers instead. */
RVV_WRAP_MASK_VV_I32(mseq, "vmseq.vv")
RVV_WRAP_MASK_VV_I32(msne, "vmsne.vv")
RVV_WRAP_MASK_VV_I32(mslt, "vmslt.vv")
RVV_WRAP_MASK_VV_I32(msle, "vmsle.vv")
RVV_WRAP_MASK_VV_I32(msltu, "vmsltu.vv")
RVV_WRAP_MASK_VV_I32(msleu, "vmsleu.vv")

static inline void rvv_merge_vvm_i32(
    int32_t* dst,
    const int32_t* a,
    const int32_t* b,
    const uint8_t* mask,
    size_t n) {
    while (n) {
        size_t vl = rvv_vsetvl_e32m1(n);
        rvv_load_i32_v16(a);
        rvv_load_i32_v8(b);
        asm volatile("vlm.v v0, (%0)\n\tvmerge.vvm v24, v8, v16, v0"
                     :
                     : "r"(mask)
                     : "memory");
        rvv_store_i32_v24(dst);
        a += vl;
        b += vl;
        dst += vl;
        mask += (vl + 7) / 8;
        n -= vl;
    }
}

static inline void
rvv_merge_eq_i32(int32_t* dst, const int32_t* a, const int32_t* b, size_t n) {
    while (n) {
        size_t vl = rvv_vsetvl_e32m1(n);
        rvv_load_i32_v16(a);
        rvv_load_i32_v8(b);
        asm volatile("vmseq.vv v0, v16, v8\n\tvmerge.vvm v24, v8, v16, v0" ::: "memory");
        rvv_store_i32_v24(dst);
        a += vl;
        b += vl;
        dst += vl;
        n -= vl;
    }
}

static inline long rvv_mseq_count_first_i32(
    const int32_t* lhs,
    const int32_t* rhs,
    size_t n,
    long* first_out) {
    long total = 0;
    long first = -1;
    long offset = 0;

    while (n) {
        long count;
        long first_in_chunk;
        size_t vl = rvv_vsetvl_e32m1(n);
        rvv_load_i32_v16(lhs);
        rvv_load_i32_v8(rhs);
        asm volatile("vmseq.vv v0, v16, v8\n\tvcpop.m %0, v0\n\tvfirst.m %1, v0"
                     : "=r"(count), "=r"(first_in_chunk)
                     :
                     : "memory");
        if (first < 0 && first_in_chunk >= 0) {
            first = offset + first_in_chunk;
        }
        total += count;
        offset += (long)vl;
        lhs += vl;
        rhs += vl;
        n -= vl;
    }

    *first_out = first;
    return total;
}

static inline void
rvv_saxpy_i32(int32_t* dst, const int32_t* x, const int32_t* y, int32_t a, size_t n) {
    while (n) {
        size_t vl = rvv_vsetvl_e32m1(n);
        rvv_load_i32_v8(x);
        rvv_load_i32_v16(y);
        asm volatile("vmul.vx v8, v8, %0\n\tvadd.vv v24, v8, v16" : : "r"(a) : "memory");
        rvv_store_i32_v24(dst);
        x += vl;
        y += vl;
        dst += vl;
        n -= vl;
    }
}

static inline void
rvv_dot_i32(int32_t* tmp, const int32_t* a, const int32_t* b, size_t n) {
    while (n) {
        size_t vl = rvv_vsetvl_e32m1(n);
        rvv_load_i32_v8(a);
        rvv_load_i32_v16(b);
        asm volatile("vmul.vv v24, v8, v16" ::: "memory");
        rvv_store_i32_v24(tmp);
        a += vl;
        b += vl;
        tmp += vl;
        n -= vl;
    }
}

/* 整型向量点积：DotProduct(A,B) = sum(A[i] * B[i])
 * 采用"向量累加器 + 标量水平归约"方案：
 *  1. 主循环内用 vmacc.vv 把当前块逐 lane 乘积累加进 v3（模拟器未实现
 *     vredsum.vs 归约指令，无法在向量寄存器内部完成整个求和）；
 *  2. 循环结束后用 vslidedown.vx + vmv.x.s 逐 lane 取出 v3 的 VLMAX 个
 *     元素，在标量寄存器里完成最终求和。
 * 注意：主循环的 vsetvli 必须用 tu/mu 尾部策略——当剩余元素不足 VLMAX
 * 时，尾部 lane 属于 tail 元素，tu 保证 v3 中上一次迭代已经累加的值
 * 不会被破坏（若用 ta/ma，尾部 lane 的旧值可能被改写导致结果错误）。 */
static inline int32_t
vector_dot_product_int32(const int32_t* a, const int32_t* b, size_t n) {
    int32_t sum = 0;
    size_t vl, vlmax;

    /* 请求 VLMAX：模拟器实现为 vl = min(avl, VLMAX)，传入最大 AVL 即得
     * 当前配置（e32, m1）下的最大向量长度 */
    asm volatile("vsetvli %0, %1, e32, m1, ta, ma" : "=r"(vlmax) : "r"(~(size_t)0));

    /* 清零累加向量 v3（vmv.v.i：立即数 0 广播到整个向量寄存器） */
    asm volatile("vmv.v.i v3, 0");

    for (; n > 0; a += vl, b += vl, n -= vl) {
        /* 动态配置向量长度：SEW=32, LMUL=1，返回本块实际处理的元素数 */
        asm volatile("vsetvli %0, %1, e32, m1, tu, mu" : "=r"(vl) : "r"(n));

        /* 将数组 a、b 当前块加载入向量寄存器 v1、v2 */
        asm volatile("vle32.v v1, (%0)" : : "r"(a) : "memory");
        asm volatile("vle32.v v2, (%0)" : : "r"(b) : "memory");

        /* 乘积累加：v3[i] = v1[i] * v2[i] + v3[i] */
        asm volatile("vmacc.vv v3, v1, v2" ::: "memory");
    }

    /* 水平归约：vslidedown.vx 把 v3 中第 i 个 lane 滑到 v4 的 lane 0，
     * 再用 vmv.x.s 读回标量寄存器 */
    for (size_t i = 0; i < vlmax; ++i) {
        int32_t lane;
        asm volatile("vslidedown.vx v4, v3, %1\n\tvmv.x.s %0, v4" : "=r"(lane) : "r"(i));
        sum += lane;
    }
    return sum;
}

static inline void rvv_slideup1_i32(int32_t* dst, const int32_t* src, size_t n) {
    size_t vl = rvv_vsetvl_e32m1(n);
    rvv_load_i32_v8(src);
    asm volatile("vmv.v.i v24, 0\n\tvslideup.vi v24, v8, 1" ::: "memory");
    rvv_store_i32_v24(dst);
}

static inline void rvv_slidedown1_i32(int32_t* dst, const int32_t* src, size_t n) {
    size_t vl = rvv_vsetvl_e32m1(n);
    rvv_load_i32_v8(src);
    asm volatile("vslidedown.vi v24, v8, 1" ::: "memory");
    rvv_store_i32_v24(dst);
}

/* 整型矩阵乘法（4×4 GEMM）：C = A × B，其中 C[i][j] = sum_k A[i][k] * B[k][j]
 * 向量化思路：C 的第 i 行 = Σ_k A[i][k] ×（B 的第 k 行）。
 * B 的每行 4 个 int32 恰好占满 e32/m1 配置下的一个向量（VLEN=128bit，VLMAX=4），
 * 因此用向量-标量乘加 vwmacc.vx 把标量 A[i][k] 与整行 B[k] 的乘积一次性累加进
 * 64 位向量累加器（widen 化避免 32 位中间溢出），最后 vnclip 收窄回 32 位存入 C。
 * 相比标量三重嵌套循环（64 次乘加逐个做），每行只需 4 条向量乘加即可并行算出
 * 16 个结果元素中的 4 个。
 *
 * 寄存器分配（e32/m1 下每个向量寄存器组占 1 个寄存器）：
 *   v8      —— B 的当前行（4×i32 = 128bit）
 *   v24-v25 —— 64 位累加器（e64/m2 双寄存器组，4×i64 = 256bit）
 *   v26     —— 收窄后的 C 行（4×i32）
 */
static inline void
rvv_matrix_mul_4x4_i32(const int32_t a[4][4], const int32_t b[4][4], int32_t c[4][4]) {
    size_t vl;
    for (size_t i = 0; i < 4; ++i) {
        /* 1. 配置 e64/m2 并清零 64 位累加器：avl=4 恰为 e64/m2 的 VLMAX，
         *    vmv.v.i 广播 0 到 v24/v25 全部 4 个 64bit lane */
        asm volatile("vsetvli %0, %1, e64, m2, ta, ma" : "=r"(vl) : "r"((size_t)4));
        asm volatile("vmv.v.i v24, 0");

        /* 2. 切回 e32/m1：SEW=32 与源数据宽度一致；vwmacc 的目的寄存器组
         *    会自动按 2*SEW=64 解释（EMUL 翻倍为 m2，即 v24-v25） */
        asm volatile("vsetvli %0, %1, e32, m1, ta, ma" : "=r"(vl) : "r"((size_t)4));

        for (size_t k = 0; k < 4; ++k) {
            /* 加载 B 的第 k 行（4 个 int32）入 v8 */
            asm volatile("vle32.v v8, (%0)" : : "r"(b[k]) : "memory");
            /* 取标量 A[i][k]，与整行 B[k] 做乘加（一次算 4 个 64bit 乘积）：
             * v24 的每个 64bit lane += sign_ext(A[i][k]) * sign_ext(v8 对应 lane) */
            int32_t aik = a[i][k];
            asm volatile("vwmacc.vx v24, %0, v8" : : "r"(aik) : "memory");
        }

        /* 3. 收窄：vnclip.wi 把 2*SEW(64) 的 v24 组按立即数 0 右移后饱和截断为
         *    SEW(32) 写入 v26；移位量为 0 即纯窄化，测试值保证不触发饱和 */
        asm volatile("vnclip.wi v26, v24, 0" ::: "memory");

        /* 4. 整行（4 个 int32）一次性存回 C[i] */
        asm volatile("vse32.v v26, (%0)" : : "r"(c[i]) : "memory");
    }
}

static inline void
rvv_gather_i32(int32_t* dst, const int32_t* src, const uint32_t* index, size_t n) {
    while (n) {
        size_t vl = rvv_vsetvl_e32m1(n);
        rvv_load_i32_v8(src);
        asm volatile("vle32.v v16, (%0)\n\tvrgather.vv v24, v8, v16"
                     :
                     : "r"(index)
                     : "memory");
        rvv_store_i32_v24(dst);
        dst += vl;
        index += vl;
        n -= vl;
    }
}

static inline void rvv_narrow_shift_i32_to_i16_m2(
    int16_t* dst,
    const int32_t* src,
    uint32_t shift,
    size_t n) {
    while (n) {
        size_t vl;
        asm volatile("vsetvli %0, %3, e32, m2, tu, mu\n\t"
                     "vle32.v v8, (%1)\n\t"
                     "vsetvli %0, %3, e16, m1, tu, mu\n\t"
                     "vnsra.wx v24, v8, %2\n\t"
                     "vse16.v v24, (%4)"
                     : "=&r"(vl)
                     : "r"(src), "r"(shift), "r"(n), "r"(dst)
                     : "memory");
        src += vl;
        dst += vl;
        n -= vl;
    }
}

static inline void
rvv_move_scalar_pair_i32(int32_t* dst, const int32_t* src, int32_t value) {
    int32_t out;
    rvv_vsetvl_e32m1(1);
    rvv_load_i32_v8(src);
    asm volatile("vmv.s.x v8, %1\n\tvmv.x.s %0, v8" : "=r"(out) : "r"(value) : "memory");
    asm volatile("sw %0, 0(%1)" : : "r"(out), "r"(dst) : "memory");
}

static inline long rvv_mask_cpop_first(const uint8_t* mask, size_t n, long* first_out) {
    long count;
    long first;
    rvv_vsetvl_e8m1(n);
    asm volatile("vlm.v v8, (%2)\n\tvcpop.m %0, v8\n\tvfirst.m %1, v8"
                 : "=r"(count), "=r"(first)
                 : "r"(mask)
                 : "memory");
    *first_out = first;
    return count;
}
