# ============================================================================
# litmus_sb_fence.S — Test 4: SB + fence，"显式同步"规则消灭本机弱结果
#
# 与 Test 3 (litmus_sb.S) 唯一的区别是 litmus 本体各加一条 fence：
#   hart0 (core0):  W(x)=1 ; fence rw,rw ; R(y) → r0
#   hart1 (core1):  W(y)=1 ; fence rw,rw ; R(x) → r1
#
# fence rw,rw 强制本核 store 全局可见之后才执行后面的 load（排空 store buffer），
# "写后读"重排被切断 → 弱结果（双零）被禁止。预期：weak = 0。
# 对照 Test 3，观察同一个程序只差两条 fence 的行为差异。
#
# 运行:  make -C test_resources run_litmus TARGET=litmus_sb_fence
# ============================================================================

    .section .text.ENTRY
    .globl _start

    .equ N_ROUNDS, 1000000

_start:
    csrr t0, mhartid
    bnez t0, hart1_entry

    # ---------- hart0：清 .bss → 释放 hart1 → driver ----------
    la sp, stack_top
    la t0, sbss
    la t1, endbss
1:  bgeu t0, t1, 2f
    sd zero, 0(t0)
    addi t0, t0, 8
    j 1b
2:  fence rw,w                # 清零对外可见（实验台同步）
    li t0, 1
    la t1, boot_go
    sd t0, 0(t1)
    fence rw,w                # release hart1
    j driver

hart1_entry:
    # ---------- hart1：等启动邮箱 → core1（纯寄存器运行，无需栈） ----------
    la t0, boot_go
1:  ld t1, 0(t0)
    fence r,r
    beqz t1, 1b
    j core1

    # ---------- driver + core0 (hart0) ----------
driver:
    li s0, N_ROUNDS           # 剩余轮数
    li s3, 0                  # 当前轮次编号
    li t2, 1                  # litmus 写入值
driver_loop:
    addi s3, s3, 1
    # 本轮复位（实验台部分）
    la t0, x
    sd zero, 0(t0)
    la t0, y
    sd zero, 0(t0)
    fence rw,w                # 复位先于 go=r 对 core1 可见
    la t0, go
    sd s3, 0(t0)              # go = r：core1 可以开始了
    # ==== litmus core0：W(x)=1 ; fence rw,rw ; R(y)→r0 ====
    la t0, x
    sd t2, 0(t0)
    fence rw,rw               # ★ Test 4：排空 store buffer，W(x) 先于 R(y) 生效
    la t0, y
    ld s5, 0(t0)              # r0
    # ======================================================
    fence rw,w                # 实验台：本轮写入先于下一轮复位生效
1:  la t0, ack
    ld t1, 0(t0)
    fence r,r
    bltu t1, s3, 1b           # 等 ack >= r
    # 判定本轮：r0==0 且 r1==0 → 弱结果
    fence r,r                 # ack>=r 之后 res1 才可信
    la t0, res1
    ld t4, 0(t0)              # r1（core1 本轮读到的 x 值）
    or t4, t4, s5             # r0 | r1 == 0 → 双零 → 弱
    bnez t4, 2f
    la t0, weak_count
    ld a0, 0(t0)
    addi a0, a0, 1
    sd a0, 0(t0)
2:  addi s0, s0, -1
    bnez s0, driver_loop

    # ---------- 汇报结果 ----------
    fence r,r
    la a0, result_msg
    call print_str
    la a0, weak_count
    ld a0, 0(a0)
    li a1, 0                  # width=0：不填充
    li a2, 32                 # pad ' '
    call print_dec
    la a0, sep_msg
    call print_str
    li a0, N_ROUNDS
    li a1, 0
    li a2, 32
    call print_dec
    la a0, nl_msg
    call print_str
    # qemu virt test-finisher → 关机退出
    li t0, 0x100000
    li t1, 0x5555
    sw t1, 0(t0)
3:  wfi
    j 3b

    # ---------- core1 (hart1) ----------
core1:
    li s0, N_ROUNDS           # 剩余轮数
    li s3, 0                  # 已完成轮数
    li t2, 1                  # litmus 写入值
core1_loop:
    # 等 go 超过已完成轮数（单调比较，无窄窗口）
1:  la t0, go
    ld s4, 0(t0)
    fence r,r
    bgeu s3, s4, 1b
    # ==== litmus core1：W(y)=1 ; fence rw,rw ; R(x)→r1 ====
    la t0, y
    sd t2, 0(t0)
    fence rw,rw               # ★ Test 4：排空 store buffer，W(y) 先于 R(x) 生效
    la t0, x
    ld t4, 0(t0)              # r1
    # ======================================================
    fence rw,w                # res1 的更新先于 ack 对 driver 可见
    la t0, res1
    sd t4, 0(t0)
    fence rw,w
    la t0, ack
    sd s4, 0(t0)              # ack = 本轮编号
    fence rw,w
    addi s3, s3, 1
    addi s0, s0, -1
    bnez s0, core1_loop
5:  wfi                       # core1 收工，原地待机
    j 5b

    # ---------- 字符串 ----------
    .section .rodata
result_msg:  .asciz "[SB + rw,rw fence]   weak = "
sep_msg:     .asciz " / "
nl_msg:      .asciz "\n"

    # ---------- 共享变量（.bss，由 hart0 启动时清零） ----------
    .section .bss
    .align 3
    .globl x, y, go, ack, res1, weak_count, boot_go
x:          .dword 0
y:          .dword 0
go:         .dword 0
ack:        .dword 0
res1:       .dword 0
weak_count: .dword 0
boot_go:    .dword 0

    # ---------- hart0 专用栈 ----------
    .section .stack
    .align 12
stack_top:
    .space 4096 * 4
