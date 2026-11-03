#!/usr/bin/env bash
# Batch-run riscv-tests isa ELFs on the HERE emulator.
#
# Each bare-metal isa test signals its result via the HTIF `tohost` symbol and
# the emulator maps it to a process exit code:
#   0   = passed
#   1   = failed
#   124 = timeout (cycle budget or `timeout(1)` wall clock)
#   other = emulator error/panic
#
# Usage:
#   ./scripts/run_isa_tests.sh [-j JOBS] [--isa ISA] [--max-cycles N] [--timeout SEC] [group-prefix...]
#
# Examples:
#   ./scripts/run_isa_tests.sh                    # all rv64* groups
#   ./scripts/run_isa_tests.sh rv64ui rv64mi      # selected groups only
#   ./scripts/run_isa_tests.sh -j 4 rv64uf        # 4 parallel jobs
set -u

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ISA_DIR="$ROOT/riscv-tests/isa"
EMU="$ROOT/target/release/here"

JOBS="$(nproc)"
ISA=""
MAX_CYCLES=50000000
TIMEOUT_SEC=120

while [[ $# -gt 0 ]]; do
    case "$1" in
        -j) JOBS="$2"; shift 2 ;;
        --isa) ISA="$2"; shift 2 ;;
        --max-cycles) MAX_CYCLES="$2"; shift 2 ;;
        --timeout) TIMEOUT_SEC="$2"; shift 2 ;;
        *) break ;;
    esac
done
PREFIXES=("$@")
if [[ ${#PREFIXES[@]} -eq 0 ]]; then
    PREFIXES=(rv64)
fi

if [[ ! -x "$EMU" ]]; then
    echo "building emulator (release)..."
    (cd "$ROOT" && cargo build --release --bin here) || exit 1
fi

# isa tests are built as extension-less files at the isa/ root.
mapfile -t ALL_ELFS < <(find "$ISA_DIR" -maxdepth 1 -type f ! -name '*.*' | sort)

TESTS=()
for elf in "${ALL_ELFS[@]}"; do
    base="$(basename "$elf")"
    for prefix in "${PREFIXES[@]}"; do
        if [[ "$base" == "$prefix"* ]]; then
            TESTS+=("$elf")
            break
        fi
    done
done

if [[ ${#TESTS[@]} -eq 0 ]]; then
    echo "no tests found for prefixes: ${PREFIXES[*]}" >&2
    exit 1
fi

OUT_DIR="$(mktemp -d /tmp/isa-tests.XXXXXX)"
trap 'rm -rf "$OUT_DIR"' EXIT

run_one() {
    local elf="$1"
    local name
    name="$(basename "$elf")"
    local -a cmd=(timeout "$TIMEOUT_SEC" "$EMU" -f elf --max-cycles "$MAX_CYCLES")
    [[ -n "$ISA" ]] && cmd+=(--isa "$ISA")
    cmd+=("$elf")
    local code=0
    "${cmd[@]}" >/dev/null 2>&1 || code=$?
    echo "$code" > "$OUT_DIR/$name"
}
export -f run_one
export EMU ISA MAX_CYCLES TIMEOUT_SEC OUT_DIR

echo "running ${#TESTS[@]} tests ($JOBS parallel, max-cycles=$MAX_CYCLES, timeout=${TIMEOUT_SEC}s)..."
printf '%s\0' "${TESTS[@]}" | xargs -0 -P "$JOBS" -I{} bash -c 'run_one "$@"' _ {}

# Aggregate results per test group (rv64ui-p-add -> rv64ui).
declare -A PASS FAIL TIMEO ERROR
fail_list=()
groups=""
for elf in "${TESTS[@]}"; do
    name="$(basename "$elf")"
    group="${name%%-*}"
    code="$(cat "$OUT_DIR/$name")"
    case "$code" in
        0) PASS["$group"]=$(( ${PASS[$group]:-0} + 1 )) ;;
        1) FAIL["$group"]=$(( ${FAIL[$group]:-0} + 1 )); fail_list+=("$name") ;;
        124) TIMEO["$group"]=$(( ${TIMEO[$group]:-0} + 1 )); fail_list+=("$name (timeout)") ;;
        *) ERROR["$group"]=$(( ${ERROR[$group]:-0} + 1 )); fail_list+=("$name (emulator exit $code)") ;;
    esac
    case " $groups " in
        *" $group "*) ;;
        *) groups="$groups $group" ;;
    esac
done

echo
printf '%-12s %6s %6s %8s %8s %8s\n' "group" "pass" "fail" "timeout" "error" "total"
total_pass=0; total_fail=0; total_timeo=0; total_err=0
for group in $(printf '%s\n' $groups | sort -u); do
    p="${PASS[$group]:-0}"; f="${FAIL[$group]:-0}"; t="${TIMEO[$group]:-0}"; e="${ERROR[$group]:-0}"
    printf '%-12s %6s %6s %8s %8s %8s\n' "$group" "$p" "$f" "$t" "$e" "$((p + f + t + e))"
    total_pass=$((total_pass + p)); total_fail=$((total_fail + f))
    total_timeo=$((total_timeo + t)); total_err=$((total_err + e))
done
total=$((total_pass + total_fail + total_timeo + total_err))
echo "----------------------------------------------------"
printf '%-12s %6s %6s %8s %8s %8s\n' "TOTAL" "$total_pass" "$total_fail" "$total_timeo" "$total_err" "$total"
if [[ $total -gt 0 ]]; then
    awk "BEGIN { printf \"pass rate: %.1f%% (%d/%d)\n\", 100*$total_pass/$total, $total_pass, $total }"
fi

if [[ ${#fail_list[@]} -gt 0 ]]; then
    echo
    echo "non-passing tests:"
    for t in "${fail_list[@]}"; do
        echo "  $t"
    done
fi

[[ $total_fail -eq 0 && $total_timeo -eq 0 && $total_err -eq 0 ]]
