ARCH ?= riscv64-unknown
SRC  ?= ./src
INC  ?= ./include
LIB  ?= ./lib
OBJ  ?= ./obj
BIN  ?= ./bin
DUMP ?= ./dump

# toolchain
RV_CC ?= $(ARCH)-elf-gcc
RV_CFLAG := -ffreestanding -nostdlib -march=rv64imfdvzicsr -mabi=lp64d -mcmodel=medany -I$(INC) -g
RV_LD ?= $(ARCH)-elf-ld
RV_LDFLAG := -static -g
RV_DUMP ?= $(ARCH)-elf-objdump
RV_DUMPFLAG := -M no-aliases -S
RV_OBJCOPY ?= $(ARCH)-elf-objcopy

# qemu（QEMU_ARCH 与工具链前缀 ARCH 解耦，机器名固定为 riscv64）
QEMU_ARCH ?= riscv64
QEMU ?= qemu-system-$(QEMU_ARCH)

# prompt
VERBOSE ?= 0
ifeq ($(VERBOSE), 0)
	Q := @
else
	Q := 
endif

RV_CC_PROMPT := RV_CC
RV_LD_PROMPT := RV_LD
RV_DUMP_PROMPT := RV_DUMP
RV_OBJCOPY_PROMPT := RV_OBJCOPY

TARGETS += main fib prime matrix_mul vector_kernels io_bench trap_test virtio_blk_test virtio_rng_test ecall_test interrupt_test float clint uart_interrupt_test custom_padd8 guess
TARGETS += monitor
# 双核 litmus 实验：链接时不带 boot.o/trap.o（litmus 自带 _start 与多核入口）
TARGETS += litmus_mp litmus_mp_fence litmus_sb litmus_sb_fence
LITMUS_LIBS_OBJ := $(OBJ)/io.o $(OBJ)/power.o
TARGET_ELFS := $(patsubst %,$(BIN)/%.elf,$(TARGETS))
TARGET_DUMPS := $(patsubst %,$(DUMP)/%.dump,$(TARGETS))
TARGET ?= main

LIBS := io log boot trap trap_handler power
LIBS_OBJ := $(patsubst %,$(OBJ)/%.o,$(LIBS))

# proctect intermediate file.
.PRECIOUS: $(OBJ)/%.o

.PHONY: all build_lib build dump_all run clean debug_makefile run_litmus
all: build dump_all
echo_cmd = printf "\033[1;34m%-10s %s\033[0m\n" "$(strip $(1))" "$(strip $(2))"

build: $(TARGET_ELFS)

# trap.o
$(OBJ)/trap.o: $(LIB)/trap.S
	@ $(call echo_cmd, $(RV_CC_PROMPT), $@)
	$(Q) mkdir -p $(OBJ) $(BIN)
	$(Q) $(RV_CC) $(RV_CFLAG) -c $< -o $@

# boot.o
$(OBJ)/boot.o: $(SRC)/boot.S
	@ $(call echo_cmd, $(RV_CC_PROMPT), $@)
	$(Q) mkdir -p $(OBJ) $(BIN)
	$(Q) $(RV_CC) $(RV_CFLAG) -c $< -o $@

# default lib file.
$(OBJ)/%.o: $(LIB)/%.c
	@ $(call echo_cmd, $(RV_CC_PROMPT), $@)
	$(Q) mkdir $(OBJ) $(BIN) -p
	$(Q) $(RV_CC) $(RV_CFLAG) -c $^ -o $@

$(BIN)/%.elf: $(OBJ)/%.o $(LIBS_OBJ) linker.ld
	@ $(call echo_cmd, $(RV_LD_PROMPT), $@)
	$(Q) mkdir -p $(OBJ) $(BIN)
	$(Q) $(RV_LD) $(RV_LDFLAG) -T linker.ld -o $@ $(filter %.o,$^)
	
# default .c file
$(OBJ)/%.o: $(SRC)/%.c
	@ $(call echo_cmd, $(RV_CC_PROMPT), $@)
	$(Q) mkdir -p $(OBJ) $(BIN)
	$(Q) $(RV_CC) $(RV_CFLAG) -c $^ -o $@

# generic .S rule（litmus_*.S 等）
$(OBJ)/%.o: $(SRC)/%.S
	@ $(call echo_cmd, $(RV_CC_PROMPT), $@)
	$(Q) mkdir -p $(OBJ) $(BIN)
	$(Q) $(RV_CC) $(RV_CFLAG) -c $^ -o $@

# litmus 双核实验：自带 _start/多核入口，只链 io.o（不用 boot/trap/power）
$(BIN)/litmus_mp.elf: $(OBJ)/litmus_mp.o $(LITMUS_LIBS_OBJ) linker.ld
	@ $(call echo_cmd, $(RV_LD_PROMPT), $@)
	$(Q) mkdir -p $(OBJ) $(BIN)
	$(Q) $(RV_LD) $(RV_LDFLAG) -T linker.ld -o $@ $(filter %.o,$^)

# M-mode monitor：boot.o(_start) + monitor.c + monitor_trap.S，只链 io/power
MON_LIBS_OBJ := $(OBJ)/io.o $(OBJ)/power.o
# monitor.o 单独用不带 v 扩展的 march 编译：GCC 会把大结构体拷贝自动向量化为
# RVV 指令，而模拟器 mstatus.VS 默认 Off，向量指令将触发非法指令异常
$(OBJ)/monitor.o: $(SRC)/monitor.c
	@ $(call echo_cmd, $(RV_CC_PROMPT), $@)
	$(Q) mkdir -p $(OBJ) $(BIN)
	$(Q) $(RV_CC) -ffreestanding -nostdlib -march=rv64imfdzicsr -mabi=lp64d -mcmodel=medany -I$(INC) -g -c $< -o $@

$(BIN)/monitor.elf: $(OBJ)/monitor.o $(OBJ)/monitor_trap.o $(OBJ)/boot.o $(MON_LIBS_OBJ) linker.ld
	@ $(call echo_cmd, $(RV_LD_PROMPT), $@)
	$(Q) mkdir -p $(OBJ) $(BIN)
	$(Q) $(RV_LD) $(RV_LDFLAG) -T linker.ld -o $@ $(filter %.o,$^)

$(BIN)/litmus_mp_fence.elf: $(OBJ)/litmus_mp_fence.o $(LITMUS_LIBS_OBJ) linker.ld
	@ $(call echo_cmd, $(RV_LD_PROMPT), $@)
	$(Q) mkdir -p $(OBJ) $(BIN)
	$(Q) $(RV_LD) $(RV_LDFLAG) -T linker.ld -o $@ $(filter %.o,$^)

$(BIN)/litmus_sb.elf: $(OBJ)/litmus_sb.o $(LITMUS_LIBS_OBJ) linker.ld
	@ $(call echo_cmd, $(RV_LD_PROMPT), $@)
	$(Q) mkdir -p $(OBJ) $(BIN)
	$(Q) $(RV_LD) $(RV_LDFLAG) -T linker.ld -o $@ $(filter %.o,$^)

$(BIN)/litmus_sb_fence.elf: $(OBJ)/litmus_sb_fence.o $(LITMUS_LIBS_OBJ) linker.ld
	@ $(call echo_cmd, $(RV_LD_PROMPT), $@)
	$(Q) mkdir -p $(OBJ) $(BIN)
	$(Q) $(RV_LD) $(RV_LDFLAG) -T linker.ld -o $@ $(filter %.o,$^)


# dump target elf
dump_all: $(TARGET_DUMPS)

$(DUMP)/%.dump: $(BIN)/%.elf
	@ $(call echo_cmd, $(RV_DUMP_PROMPT), $@)
	$(Q) mkdir -p $(DUMP)
	$(Q) $(RV_DUMP) -d $(RV_DUMPFLAG) $< > $@


qemu_run: build
	$(QEMU) -machine virt -bios $(BIN)/$(TARGET).elf -serial stdio

# 双核 litmus 实验：-smp 2 两个 hart；结尾写 test-finisher(0x100000) 自动关机
# 用法: make -C test_resources run_litmus TARGET=litmus_mp
run_litmus: build
	$(QEMU) -machine virt -nographic -bios $(BIN)/$(TARGET).elf -smp 2

clean:
	rm -r $(BIN) $(OBJ)