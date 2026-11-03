# ============================================================================
# litmus_mp.S — Test 1: MP（消息传递）无 fence，观测 RVWMO 弱内存序
#
# 实验模型（MP litmus，双 hart）：
#   hart0 (writer):  W(data)=1 ; W(flag)=1       ← 两条 store 之间没有 fence
#   hart1 (reader):  R(flag) 直到 1 ; R(data)    ← 两条 load 之间没有 fence
#
#   顺序一致(SC)下只可能出现：读到的 data 与 flag 一致。
#   RVWMO 允许弱结果：reader 看到 flag==1 但 data==0（x86-TSO 上不可能）。
#
# 运行:  make -C test_resources run_litmus TARGET=litmus_mp
# 观察:  weak = 观测到的弱结果次数 / 总轮数
#
# 握手协议（实验台，单调轮次编号，无窄窗口，任何调度下都不死锁）：
#   writer: 每轮发布 go=r（递增）；reader: 观察到 go > 已完成轮数后开始本轮；
#   reader: ack=本轮编号；writer: 等 ack >= r 才进入下一轮。
# ============================================================================

    .section .text.ENTRY
    .globl _start

    .equ N_ROUNDS, 1000000

_start:
    csrr t0, mhartid
    bnez t0, hart1_entry

    # ---------- hart0：清 .bss → 释放 hart1 → writer ----------
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
    j writer

hart1_entry:
    # ---------- hart1：等启动邮箱 → reader（纯寄存器运行，无需栈） ----------
    la t0, boot_go
1:  ld t1, 0(t0)
    fence r,r                 # 实验台同步：保证后续读到清零后的共享变量
    beqz t1, 1b
    j reader

    # ---------- writer (hart0) ----------
writer:
    li s0, N_ROUNDS           # 剩余轮数
    li s3, 0                  # 当前轮次编号（单调递增）
    li t2, 1                  # litmus 写入值
writer_loop:
    addi s3, s3, 1
    # 本轮复位（实验台部分）
    la t0, flag
    sd zero, 0(t0)
    la t0, data
    sd zero, 0(t0)
    fence rw,w                # 复位必须先于 go=r 对 reader 可见
    la t0, go
    sd s3, 0(t0)              # go = r：reader 可以开始了
    # ==== litmus 写侧：W(data)=1 ; W(flag)=1，故意不加 fence ====
    la t0, data
    sd t2, 0(t0)
    la t0, flag
    sd t2, 0(t0)
    # ============================================================
    fence rw,w                # 实验台：本轮 litmus 写入先于后续复位对外生效
1:  la t0, ack
    ld t1, 0(t0)
    fence r,r
    bltu t1, s3, 1b           # 等 ack 达到本轮编号（旧值 < r 继续等）
    addi s0, s0, -1
    bnez s0, writer_loop

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
2:  wfi
    j 2b

    # ---------- reader (hart1) ----------
reader:
    li s0, N_ROUNDS           # 剩余轮数
    li s3, 0                  # 已完成轮数
    li s4, 0                  # 已观察到的 go 值
reader_loop:
    # 等 go 超过已完成轮数（实验台部分，单调比较，无窄窗口）
1:  la t0, go
    ld s4, 0(t0)
    fence r,r
    bgeu s3, s4, 1b           # go <= 已完成轮数 → 继续等
    # ==== litmus 读侧：R(flag) 直到 1，再 R(data)，故意不加 fence ====
2:  la t0, flag
    ld t3, 0(t0)
    beqz t3, 2b
    la t1, data
    ld t4, 0(t1)
    # ============================================================
    bnez t4, 3f               # data==1：符合顺序预期
    la t0, weak_count         # 弱结果：flag==1 但 data==0
    ld a0, 0(t0)
    addi a0, a0, 1
    sd a0, 0(t0)
3:  fence rw,w                # 实验台：weak_count 的更新先于 ack 对 writer 可见
    la t0, ack
    sd s4, 0(t0)              # ack = 本轮编号
    fence rw,w
    addi s3, s3, 1
    addi s0, s0, -1
    bnez s0, reader_loop
5:  wfi                       # reader 收工，原地待机
    j 5b

    # ---------- 字符串 ----------
    .section .rodata
result_msg:  .asciz "[MP no-fence]       weak = "
sep_msg:     .asciz " / "
nl_msg:      .asciz "\n"

    # ---------- 共享变量（.bss，由 hart0 启动时清零） ----------
    .section .bss
    .align 3
    .globl flag, data, go, ack, weak_count, boot_go
flag:       .dword 0
data:       .dword 0
go:         .dword 0
ack:        .dword 0
weak_count: .dword 0
boot_go:    .dword 0

    # ---------- hart0 专用栈 ----------
    .section .stack
    .align 12
stack_top:
    .space 4096 * 4
