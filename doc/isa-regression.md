# riscv-tests 回归测试框架

> 2026-09-09 添加。让模拟器直接裸跑 [riscv-tests](../riscv-tests) 的 isa 测试，
> 通过 HTIF `tohost` 协议判定通过/失败，并批量生成通过率报告。
> 这是模拟器正确性的黄金标准回归：每次改动模拟器后跑一遍，通过率的变化就是改动质量的最直接反馈。

## 一、改了什么

### 1. `src/main.rs` —— CLI 支持 tohost 退出

之前的问题：riscv-tests 的测试写完 `tohost` 后会自旋死循环（`write_tohost: j write_tohost`），
而 CLI 的 `run_normal` 只会 `board.run()`，所以裸跑测试会永远挂住。

改动：

- 新增 `run_tohost_polling()`：
  - 运行前检查 ELF 是否含 `.tohost` 节（复用 `ELFLoader::get_section_addr`）；
  - 有则每 **4096 周期**轮询一次 `tohost`（复用 `tests/riscv_tests.rs` 集成测试的既有模式）；
  - `tohost == 1` → 打印 `PASSED`，进程退出码 `0`；
  - `tohost == (n<<1)|1`（奇数 >1）→ 打印 `FAILED (testnum=n)`，退出码 `1`（n 是测试内部失败用例编号）；
  - `--max-cycles` 耗尽仍未写入 → 打印 `TIMEOUT after N cycles at pc ...`，退出码 `124`；
  - 板子先 Halt 但没写 tohost → `FAILED (halted before writing tohost)`，退出码 `1`。
- 无 `.tohost` 节的输入（如 OpenSBI `fw_payload.bin`、test_resources 的裸机程序）**行为完全不变**，仍走 `board.run()`。
- `--format auto` 未知扩展名时不再直接 panic，而是先按内容嗅探：像 ELF 就按 ELF 加载，否则按 raw bin。
  所以现在可以直接 `./target/release/here riscv-tests/isa/rv64ui-p-add`（测试 ELF 没有扩展名）。
- raw mode 守卫只在 stdin/stdout 都是真实终端时启用，批量/管道环境下不会因开 raw mode 失败而退出。
- `--signature`（riscv-arch-test 用）依然在退出前导出，不受影响。

### 2. `scripts/run_isa_tests.sh`（新增）—— 批量回归

- 收集 `riscv-tests/isa/` 根目录下无扩展名的测试 ELF（构建产物布局）；
- 默认只跑 `rv64*` 组（rv32 ELF 无法在 RV64 模拟器上运行）；
- 用 `xargs -P` 并发运行，每个测试套 `timeout(1)` 双保险；
- 按退出码分类：`0`=pass、`1`=fail、`124`=timeout、其它=emulator error；
- 按组（`rv64ui-p-add` → `rv64ui`）汇总表格，列出全部非通过测试。

### 3. `Makefile` —— 新增 `isa-tests` target

```make
isa-tests:
	cargo build --release --bin here
	./scripts/run_isa_tests.sh -j $(JOBS) $(ISA_ARGS)
```

## 二、使用说明

### 单跑一个测试

```bash
cargo build --release
./target/release/here riscv-tests/isa/rv64ui-p-add          # auto 格式，直接跑
./target/release/here -f elf riscv-tests/isa/rv64ui-p-add   # 显式指定 ELF
./target/release/here -f elf --max-cycles 50000000 <elf>    # 限制周期预算
echo $?   # 0=通过 1=失败 124=超时
```

### 批量回归

```bash
./scripts/run_isa_tests.sh                    # 全部 rv64* 组（约 356 个）
./scripts/run_isa_tests.sh rv64ui rv64mi      # 只跑指定组
./scripts/run_isa_tests.sh -j 4 rv64uf        # 指定并发数
./scripts/run_isa_tests.sh --isa RV64GC rv64ui  # 覆盖 ISA 字符串
./scripts/run_isa_tests.sh --max-cycles 100000000 --timeout 300   # 调大预算
make isa-tests                                # 等价：先构建再全量跑
make isa-tests ISA_ARGS="rv64ui rv64uc"       # make 方式指定组
```

脚本参数：

| 参数 | 默认值 | 说明 |
|---|---|---|
| `-j JOBS` | `nproc` | 并发进程数 |
| `--isa ISA` | 内置默认（RV64GC） | 传给模拟器的 ISA 字符串 |
| `--max-cycles N` | 50000000 | 每个测试的模拟周期上限 |
| `--timeout SEC` | 120 | 每个测试的墙钟上限（`timeout(1)`） |

### 输出示例

```
group          pass   fail  timeout    error    total
rv64si            7      0        0        0        7
rv64ui          106      1        1        0      108
----------------------------------------------------
TOTAL           242     90       24        0      356
pass rate: 68.0% (242/356)

non-passing tests:
  rv64ui-p-ma_data
  rv64uzbb-p-andn
  ...
```

## 三、当前基线（2026-09-09）

**242/356 = 68.0%**

| 组 | 通过 | 未通过 | 缺失能力 |
|---|---|---|---|
| rv64ui | 107/108 | ma_data | 非对齐 load/store 处理 |
| rv64um/ua/uf/ud/uc | 全部 | 无 | —— |
| rv64si | 7/7 | 无 | —— |
| rv64mi | 15/17 | breakpoint, pmpaddr | 断点陷阱、PMP |
| rv64uzbb | 0/48 | 全部 | **Zbb 位操作扩展未实现** |
| rv64uzba | 0/16 | 全部 | Zba（address generation）未实现 |
| rv64uzbs | 0/16 | 全部 | Zbs（single-bit ops）未实现 |
| rv64uzbc | 0/6 | 全部 | Zbc（carry-less multiply）未实现 |
| rv64uzfh | 2/22 | 大部分 | Zfh 半精度浮点未实现 |
| rv64mzicbo / rv64ssvnapot / rv64uziccid | 0 | 全部 | CMO / NAPOT 页 / 一致性指令 |

注：`-v` 后缀是带虚拟内存环境的变体，跟随对应 `-p` 组的能力。

## 四、建议的实现顺序（即学习路线）

1. **Zbb/Zba/Zbs 位操作**（86 个测试）：指令语义清晰，解码 + 一个执行函数即可点亮一个组，性价比最高；
2. **非对齐访问**（ma_data）：在 store/load 路径按字节拆分，或按规格注入异常；
3. **Zfh 半精度**（20 个测试）：软浮点框架已有 F/D，补充半精度转换与运算；
4. **PMP + 断点陷阱**：CSR 与陷阱控制器进阶。

## 五、注意事项

- tohost 轮询粒度是 4096 周期，`--max-cycles` 小于一个粒度时仍可能完成短测试（轮询批次先跑完再检查）。
- 修改 `riscv-tests/` 后需要重新 `make` 该目录，再跑回归。
- 集成测试入口 `cargo test --test riscv_tests -- --nocapture` 仍然可用（feature `riscv-tests` 在 default 里）。
- rv32 组默认不跑；如需尝试：`./scripts/run_isa_tests.sh rv32`（预期全部失败，因为解码器是 RV64）。
