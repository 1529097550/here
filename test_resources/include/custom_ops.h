#ifndef CUSTOM_OPS_H
#define CUSTOM_OPS_H

#include <stdint.h>

// packed-simd 指令家族 (custom-0 空间, opcode=0x0b)
// .insn r 参数顺序: opcode, funct3, funct7
// funct3 选泳道宽度 (0=8bit x4, 1=16bit x2), funct7 选运算 (0=加, 1=减)

static inline uint32_t padd8(uint32_t a, uint32_t b) {
    uint32_t result;
    asm volatile (
        ".insn r 0x0b, 0, 0, %0, %1, %2"
        : "=r"(result)
        : "r"(a), "r"(b)
    );
    return result;
}

static inline uint32_t psub8(uint32_t a, uint32_t b) {
    uint32_t result;
    asm volatile (".insn r 0x0b, 0, 1, %0, %1, %2"
                  : "=r"(result) : "r"(a), "r"(b));
    return result;
}

static inline uint32_t padd16(uint32_t a, uint32_t b) {
    uint32_t result;
    asm volatile (".insn r 0x0b, 1, 0, %0, %1, %2"
                  : "=r"(result) : "r"(a), "r"(b));
    return result;
}

static inline uint32_t psub16(uint32_t a, uint32_t b) {
    uint32_t result;
    asm volatile (".insn r 0x0b, 1, 1, %0, %1, %2"
                  : "=r"(result) : "r"(a), "r"(b));
    return result;
}

#endif // CUSTOM_OPS_H
