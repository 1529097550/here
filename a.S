# 用户态最小程序：qemu-riscv64 直接加载
# -elf 工具链没有 C 运行时（crt0/libc），不能用 main/printf，
# 必须自带 _start，输出和退出都走 Linux 系统调用（ecall）

.section .text
.globl _start

_start:
    li   s0, 0              # 用 s0 作计数器

1:
    li   a7, 64             # 系统调用 64 = write
    li   a0, 1              # fd = 1 (stdout)
    la   a1, msg            # 字符串地址
    li   a2, 14             # 长度
    ecall

    addi s0, s0, 1
    li   t1, 10
    bne  s0, t1, 1b         # 循环 10 次

    li   a7, 93             # 系统调用 93 = exit
    li   a0, 0              # 退出码 0
    ecall

.section .rodata
msg:
    .ascii "Hello, world!\n"
