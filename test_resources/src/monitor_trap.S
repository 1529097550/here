# monitor_trap.S —— 小型 M-mode monitor 的 trap 入口与上下文恢复
#
# 约定：mscratch 永远保存专用 trap stack 栈顶。
#   - trap 发生时 csrrw 把当前 sp 换出到 mscratch、换入 trap stack，
#     因此无论 trap 来自 U-mode 还是 M-mode，trap frame 都构建在专用栈上，
#     不会破坏被中断上下文的栈。
#   - __mon_restore 恢复前无条件把 mscratch 重置回 trap stack 栈顶，
#     保证不变式在任意恢复路径下都成立。
#
# trap frame 布局（与 monitor.c 中 trap_frame_t 严格一致，共 288 字节）：
#   offset 0    : x[1]  (ra)
#   offset i*8  : x[i]  (i = 1..31, x2 = sp)
#   offset 256  : mepc
#   offset 264  : mstatus
#   offset 272  : mcause
#   offset 280  : mtval

.altmacro
.macro SAVE_REG n
    sd x\n, \n*8(sp)
.endm
.macro LOAD_REG n
    ld x\n, \n*8(sp)
.endm

.set FRAME_SIZE, 288

.globl __mon_trap_entry
.globl __mon_restore
.align 2

# M-mode trap 入口（mtvec 指向这里）
__mon_trap_entry:
    csrrw sp, mscratch, sp        # sp <- trap stack 栈顶; mscratch <- 被中断的 sp
    addi sp, sp, -FRAME_SIZE
    # 保存 x1..x31（t0/t1/t2 尚未被使用，先保存再用）
    .set n, 1
    .rept 31
        SAVE_REG %n
        .set n, n+1
    .endr
    csrr t0, mscratch             # 被中断的 sp，补写 x2 槽位
    sd t0, 2*8(sp)
    # 保存 CSRs
    csrr t0, mepc
    csrr t1, mstatus
    csrr t2, mcause
    sd t0, 32*8(sp)
    sd t1, 33*8(sp)
    sd t2, 34*8(sp)
    csrr t0, mtval
    sd t0, 35*8(sp)
    # a0 = trap frame 指针，调用 C trap handler；返回值 a0 = 待恢复任务的 frame
    mv a0, sp
    call monitor_trap_handler
    # fallthrough 到 __mon_restore

# 恢复 a0 指向的 trap frame 并 mret。
# 用途一：trap_handler 返回后恢复某任务上下文（可能是不同任务 => 上下文切换）。
# 用途二：monitor main 首次调用它，直接从 M-mode 启动第一个 U-mode 任务。
__mon_restore:
    mv sp, a0
    ld t0, 32*8(sp)               # mepc
    ld t1, 33*8(sp)               # mstatus
    csrw mepc, t0
    csrw mstatus, t1
    la t2, __mon_trap_stack_top
    ld t2, 0(t2)
    csrw mscratch, t2             # 重置不变式：mscratch 恒为 trap stack 栈顶
    # 恢复 x31..x3（含 a0/x10），x0 硬连线无需恢复
    .set n, 31
    .rept 29
        LOAD_REG %n
        .set n, n-1
    .endr
    ld x1, 1*8(sp)                # ra
    ld x2, 2*8(sp)                # sp 最后恢复
    mret
