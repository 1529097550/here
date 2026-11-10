// monitor.c —— 小型 M-mode monitor：多道批处理 + 优先级抢占调度
//
// 职责：
//   1. 设置 mtvec/mscratch/mie，配置 CLINT 定时器时间片；
//   2. 从 M-mode 启动多个 U-mode 任务（通过 __mon_restore 直接 mret 进 U-mode）；
//   3. trap handler 处理：
//      - U-mode ecall 系统调用（字符输出 / sleep / spawn / yield / exit）；
//      - 非法指令：打印 mcause/mepc/mtval 后跳过该指令；
//      - 非法访存：打印后终止出错任务并切换到下一个任务；
//      - CLINT 机器定时器中断：重置 mtimecmp 并重新调度（时间片轮转）；
//   4. 优先级抢占调度：总是运行就绪的最高优先级任务，同优先级按 last_run
//      轮转保证公平；睡眠任务不参与调度，由定时器驱动的调度点唤醒。

#include "io.h"
#include "power.h"
#include <stddef.h>
#include <stdint.h>

// ========================= trap frame（与 monitor_trap.S 一致） =========================
typedef struct trap_frame {
    uint64_t x[32];    // x[i] 在 i*8 偏移处；x[0] 槽位保留，x[2] = sp
    uint64_t mepc;     // 32*8 = 256
    uint64_t mstatus;  // 33*8 = 264
    uint64_t mcause;   // 34*8 = 272
    uint64_t mtval;    // 35*8 = 280
} trap_frame_t;
_Static_assert(sizeof(trap_frame_t) == 288, "trap frame must match monitor_trap.S");

// trap frame 中常用寄存器槽位
#define FRAME_A0 10  // x10
#define FRAME_A1 11  // x11
#define FRAME_A7 17  // x17

// freestanding 环境下 GCC 会为大结构体拷贝生成 memcpy 调用，需自行提供
void* memcpy(void* dst, const void* src, unsigned long n) {
    uint8_t* d = dst;
    const uint8_t* s = src;
    while (n--)
        *d++ = *s++;
    return dst;
}

void* memset(void* dst, int c, unsigned long n) {
    uint8_t* d = dst;
    while (n--)
        *d++ = (uint8_t)c;
    return dst;
}

// ========================= CSR 访问 =========================
static inline uint64_t r_mstatus(void) {
    uint64_t v;
    asm volatile("csrr %0, mstatus" : "=r"(v));
    return v;
}
static inline void w_mstatus(uint64_t v) { asm volatile("csrw mstatus, %0" ::"r"(v)); }
static inline void w_mie(uint64_t v) { asm volatile("csrw mie, %0" ::"r"(v)); }
static inline void w_mtvec(uint64_t v) { asm volatile("csrw mtvec, %0" ::"r"(v)); }
static inline void w_mscratch(uint64_t v) { asm volatile("csrw mscratch, %0" ::"r"(v)); }

// ========================= CLINT（QEMU virt / HERE 模拟器内存映射一致） =========================
#define CLINT_MTIMECMP0 0x02004000UL  // hart0 mtimecmp
#define CLINT_MTIME 0x0200BFF8UL      // mtime
#define TIME_SLICE 100000UL           // 一个时间片 = 100000 个 mtime tick（mtime = 模拟器 cycle>>3）

static inline uint64_t mtime_read(void) { return *(volatile uint64_t*)CLINT_MTIME; }
static inline void mtimecmp_set(uint64_t v) { *(volatile uint64_t*)CLINT_MTIMECMP0 = v; }

// ========================= U-mode 系统调用接口 =========================
#define SYS_PUTCHAR 1  // a0 = 字符
#define SYS_SLEEP 2    // a0 = 睡眠的时间片数
#define SYS_SPAWN 3    // a0 = 入口函数, a1 = 优先级；返回 tid
#define SYS_EXIT 4     // a0 = 退出码
#define SYS_YIELD 5    // 主动让出 CPU

static inline long sys_call(long nr, long a0, long a1) {
    register long _a7 asm("a7") = nr;
    register long _a0 asm("a0") = a0;
    register long _a1 asm("a1") = a1;
    asm volatile("ecall" : "+r"(_a0) : "r"(_a7), "r"(_a1) : "memory");
    return _a0;
}

#define sys_putc(c) sys_call(SYS_PUTCHAR, (long)(unsigned char)(c), 0)
#define sys_sleep(ticks) sys_call(SYS_SLEEP, (long)(ticks), 0)
#define sys_spawn(entry, prio) sys_call(SYS_SPAWN, (long)(entry), (long)(prio))
#define sys_exit(code) sys_call(SYS_EXIT, (long)(code), 0)
#define sys_yield() sys_call(SYS_YIELD, 0, 0)

static void sys_print(const char* s) {
    while (*s)
        sys_putc(*s++);
}

// ========================= 专用 trap stack =========================
// 与任务栈、monitor 主栈完全分离，trap frame 恒建于其上（见 monitor_trap.S）
static uint8_t mon_trap_stack[4096] __attribute__((aligned(16)));
uint64_t __mon_trap_stack_top = (uint64_t)(mon_trap_stack + sizeof(mon_trap_stack));

// ========================= 任务与调度器 =========================
#define MAX_TASKS 16
#define TASK_STACK_SIZE 4096

typedef enum { T_UNUSED, T_READY, T_SLEEPING, T_DEAD } task_state_t;

typedef struct task {
    trap_frame_t frame;
    uint8_t stack[TASK_STACK_SIZE] __attribute__((aligned(16)));
    task_state_t state;
    int prio;        // 优先级，越大越高
    uint64_t wake_at;        // 睡眠截止时刻（mtime 值）
    uint64_t last_run;       // 最近一次被调度的 sched_tick，同优先级按此轮转
} task_t;

static task_t tasks[MAX_TASKS];
static task_t* cur = NULL;  // 当前正在运行的任务（trap 发生时指向它）
static uint64_t sched_tick = 0;
static uint64_t ctx_switches = 0;

static const char* state_name(task_state_t s) {
    switch (s) {
    case T_READY: return "READY";
    case T_SLEEPING: return "SLEEPING";
    case T_DEAD: return "DEAD";
    default: return "UNUSED";
    }
}

// 创建任务：初始化 trap frame，使其第一次 mret 即进入 U-mode 入口
static task_t* spawn_task(void (*entry)(void), int prio) {
    for (int i = 0; i < MAX_TASKS; i++) {
        if (tasks[i].state != T_UNUSED && tasks[i].state != T_DEAD)
            continue;
        task_t* t = &tasks[i];
        for (int j = 0; j < 32; j++)
            t->frame.x[j] = 0;
        t->frame.mepc = (uint64_t)entry;
        // MPIE=1：mret 后 MIE<-1 使机器定时器中断在 U-mode 生效；
        // MPP=0：mret 后进入 U-mode
        t->frame.mstatus = 1UL << 7;
        t->frame.mcause = 0;
        t->frame.mtval = 0;
        t->frame.x[2] = (uint64_t)(t->stack + TASK_STACK_SIZE);  // 任务栈顶，16 对齐
        t->state = T_READY;
        t->prio = prio;
        t->wake_at = 0;
        t->last_run = 0;
        return t;
    }
    printf("[monitor] spawn failed: no free task slot\n");
    return NULL;
}

static void wake_expired(void) {
    uint64_t now = mtime_read();
    for (int i = 0; i < MAX_TASKS; i++)
        if (tasks[i].state == T_SLEEPING && now >= tasks[i].wake_at) {
            tasks[i].state = T_READY;
            printf("[monitor] task %d woke up (prio %d)\n", i, tasks[i].prio);
        }
}

static int pick_next(void) {
    int best = -1;
    for (int i = 0; i < MAX_TASKS; i++) {
        if (tasks[i].state != T_READY)
            continue;
        if (best < 0 || tasks[i].prio > tasks[best].prio ||
            (tasks[i].prio == tasks[best].prio && tasks[i].last_run < tasks[best].last_run))
            best = i;
    }
    return best;
}

static int all_dead(void) {
    for (int i = 0; i < MAX_TASKS; i++)
        if (tasks[i].state != T_DEAD && tasks[i].state != T_UNUSED)
            return 0;
    return 1;
}

// 调度器：保存当前上下文 -> 唤醒到期睡眠任务 -> 选择下一个任务。
// 返回待恢复的 trap frame；全部任务结束时返回 NULL。
// 注意：handler 内 mstatus.MIE=0，不会嵌套 trap，故此处轮询 mtime 是安全的。
static trap_frame_t* schedule(trap_frame_t* tf) {
    if (cur && cur->state != T_DEAD)
        cur->frame = *tf;  // 保存当前任务上下文
    cur = NULL;
    ctx_switches++;
    for (;;) {
        wake_expired();
        int next = pick_next();
        if (next >= 0) {
            tasks[next].last_run = ++sched_tick;
            cur = &tasks[next];
            printf("[monitor] sched tick %llx -> task %d (%s, prio %d)\n",
                   (unsigned long long)sched_tick, next, state_name(tasks[next].state),
                   tasks[next].prio);
            return &cur->frame;
        }
        if (all_dead())
            return NULL;
        // 无任务可运行：空转等待（此处避免嵌套开中断，靠轮询 mtime 推进唤醒检查）
        uint64_t deadline = mtime_read() + TIME_SLICE / 4;
        while (mtime_read() < deadline)
            ;
    }
}

// ========================= trap handler =========================
static void print_trap_info(const char* tag, trap_frame_t* tf) {
    printf("[monitor] %s: task %ld mcause=%llx mepc=%llx mtval=%llx\n", tag,
           cur ? (long)(cur - tasks) : -1L, (unsigned long long)tf->mcause,
           (unsigned long long)tf->mepc, (unsigned long long)tf->mtval);
}

// 返回待恢复的 trap frame 指针（可能是原任务，也可能是调度后的新任务）。
// monitor_trap.S 在 call 之后直接 fallthrough 到 __mon_restore，
// 因此 handler 必须通过 a0 把 frame 指针带回。
trap_frame_t* monitor_trap_handler(trap_frame_t* tf) {
    uint64_t cause = tf->mcause;
    int need_sched = 0;

    if ((long long)cause < 0) {  // bit63 = 1：中断
        uint64_t code = cause & ~(1UL << 63);
        if (code == 7) {  // 机器定时器中断：时间片到
            mtimecmp_set(mtime_read() + TIME_SLICE);
            need_sched = 1;
        } else {
            print_trap_info("unexpected interrupt", tf);
            need_sched = 1;
        }
    } else {
        switch (cause) {
        case 8: {  // U-mode ecall
            task_t* t = cur;
            long nr = (long)tf->x[FRAME_A7];
            long a0 = (long)tf->x[FRAME_A0];
            long a1 = (long)tf->x[FRAME_A1];
            switch (nr) {
            case SYS_PUTCHAR:
                uart_putc((char)a0);
                tf->mepc += 4;  // 返回原位置（跳过 ecall 本身）
                return tf;
            case SYS_SLEEP:
                // 睡眠期间不参与调度；由时间片推进的调度点唤醒
                t->state = T_SLEEPING;
                t->wake_at = mtime_read() + (uint64_t)a0 * TIME_SLICE;
                tf->mepc += 4;  // 唤醒后从 ecall 的下一条指令继续
                printf("[monitor] task %ld sleeps %ld slice(s)\n", (long)(t - tasks), a0);
                need_sched = 1;
                break;
            case SYS_SPAWN: {
                task_t* nt = spawn_task((void (*)(void))a0, (int)a1);
                tf->x[FRAME_A0] = nt ? (long)(nt - tasks) : -1;
                tf->mepc += 4;
                if (nt && nt->prio > t->prio)
                    need_sched = 1;  // 新任务优先级更高：立即抢占
                else
                    return tf;
                break;
            }
            case SYS_EXIT:
                printf("[monitor] task %ld exited, code=%ld\n", (long)(t - tasks), a0);
                t->state = T_DEAD;
                need_sched = 1;
                break;
            case SYS_YIELD:
                tf->mepc += 4;  // 让出后从 ecall 的下一条指令继续
                need_sched = 1;
                break;
            default:
                printf("[monitor] task %ld unknown syscall %ld\n", (long)(t - tasks), nr);
                tf->mepc += 4;
                return tf;
            }
            break;
        }
        case 2:  // 非法指令：打印信息后跳过该指令继续执行
            print_trap_info("illegal instruction -> skipped", tf);
            tf->mepc += 4;
            return tf;
        case 4: case 5: case 6: case 7: {  // 访存异常：终止出错任务
            print_trap_info("memory fault -> task killed", tf);
            if (cur)
                cur->state = T_DEAD;
            need_sched = 1;
            break;
        }
        default:  // 其它异常：保守起见终止当前任务
            print_trap_info("unhandled exception -> task killed", tf);
            if (cur)
                cur->state = T_DEAD;
            need_sched = 1;
            break;
        }
    }

    if (!need_sched)
        return tf;
    tf = schedule(tf);
    if (tf == NULL) {
        int n_dead = 0;
        for (int i = 0; i < MAX_TASKS; i++)
            if (tasks[i].state == T_DEAD)
                n_dead++;
        printf("=== monitor: all %d tasks finished (sched ticks=%llx, ctx switches=%llx) ===\n",
               n_dead, (unsigned long long)sched_tick, (unsigned long long)ctx_switches);
        for (int i = 0; i < MAX_TASKS; i++)
            if (tasks[i].state != T_UNUSED)
                printf("  task %d: %s (prio %d)\n", i, state_name(tasks[i].state), tasks[i].prio);
        PowerOff();
    }
    return tf;
}

// ========================= U-mode 任务示例 =========================
static void task_d(void) {
    sys_print("[task D] running with HIGH priority (preempted A right after spawn)\n");
    sys_exit(0);
}

static void task_a(void) {
    sys_print("[task A] hello\n");
    sys_sleep(2);
    sys_print("[task A] woke up after 2 slices\n");
    long tid = sys_spawn(task_d, 3);
    sys_print("[task A] spawned task D(prio 3), tid returned\n");
    (void)tid;
    sys_print("[task A] resumed after D finished, bye\n");
    sys_exit(0);
}

static void task_b(void) {
    sys_print("[task B] hello, now triggering an ILLEGAL INSTRUCTION\n");
    asm volatile(".word 0xffffffff");  // 非法指令：monitor 打印后跳过
    sys_print("[task B] survived illegal instruction (skipped by monitor)\n");
    sys_exit(0);
}

static void task_c(void) {
    sys_print("[task C] hello, now triggering a LOAD ACCESS FAULT\n");
    *(volatile int*)0x100;  // 未映射地址 -> load access fault，任务被 monitor 终止
    sys_print("[task C] NEVER printed: task killed by monitor\n");
    sys_exit(0);
}

static void task_e(void) {
    for (int i = 0; i < 3; i++) {
        sys_print("[task E] yield round ");
        sys_putc('0' + i);
        sys_putc('\n');
        sys_yield();
    }
    sys_print("[task E] done\n");
    sys_exit(0);
}

// ========================= monitor 入口 =========================
extern void __mon_trap_entry(void);
extern void __mon_restore(trap_frame_t*);

int main(void) {
    printf("=== M-mode monitor booting ===\n");

    // 1. trap 基础设施：mtvec 指向入口，mscratch 恒存 trap stack 栈顶
    w_mtvec((uint64_t)__mon_trap_entry);
    w_mscratch(__mon_trap_stack_top);

    // 2. 使能机器定时器中断，设置第一个时间片到期时间
    //    注意 mstatus.MIE 保持 0：首任务通过 frame 的 MPIE=1 在 mret 时开启
    w_mie(1UL << 7);  // MTIE
    mtimecmp_set(mtime_read() + TIME_SLICE);

    // 3. 创建 U-mode 任务（A/B/C/E 同优先级公平轮转，D 用于优先级抢占演示）
    spawn_task(task_a, 1);
    spawn_task(task_b, 1);
    spawn_task(task_c, 1);
    spawn_task(task_e, 1);

    printf("=== monitor: 4 tasks ready, entering U-mode ===\n");

    // 4. 选择第一个任务，从 M-mode 直接 mret 进入 U-mode 执行
    int first = pick_next();
    tasks[first].last_run = ++sched_tick;
    cur = &tasks[first];
    __mon_restore(&cur->frame);

    // 不会回到这里：调度全部发生在 trap handler 内
    printf("!!! unreachable !!!\n");
    PowerOff();
}
