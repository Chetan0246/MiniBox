#!/usr/bin/env bash
# MiniBox Phase 7 Automated Test Suite
# Tests benchmarking engine, evaluation metrics, and reporting

set -uo pipefail

BENCHMARK_SCRIPT="./benchmarks/benchmark.py"
RESULTS_JSON="./benchmarks/results.json"
REPORT_MD="./benchmarks/PERFORMANCE_REPORT.md"
CORE_TARGET="./bin/minibox"
FAILED=0
PASSED=0

RED='\033[0;31m'
GREEN='\033[0;32m'
BLUE='\033[0;34m'
NC='\033[0m'

pass() {
    echo -e "${GREEN}[PASS]${NC} $1"
    PASSED=$((PASSED + 1))
}

fail() {
    echo -e "${RED}[FAIL]${NC} $1: $2"
    FAILED=$((FAILED + 1))
}

info() {
    echo -e "${BLUE}[INFO]${NC} $1"
}

cleanup() {
    "$CORE_TARGET" cleanup >/dev/null 2>&1 || true
}
trap cleanup EXIT

echo "=================================================="
echo "Running MiniBox Phase 7 Test Suite"
echo " (Testing & Performance Evaluation)"
echo "=================================================="

# Test 1: Benchmark script presence and execution bit
info "Test 1: Benchmark script presence and permissions"
if [ -x "$BENCHMARK_SCRIPT" ]; then
    pass "Benchmark engine $BENCHMARK_SCRIPT exists and is executable"
else
    fail "Benchmark engine" "$BENCHMARK_SCRIPT is missing or not executable"
fi

TEST_RESULTS_JSON="/tmp/minibox_test_phase7_results.json"
TEST_REPORT_MD="/tmp/minibox_test_phase7_report.md"

# Test 2: Execute benchmark suite in quick mode
info "Test 2: Execute benchmark suite (multi-trial quick mode)"
PYTHON_EXEC=".venv/bin/python3"
if [ ! -x "$PYTHON_EXEC" ]; then
    PYTHON_EXEC="$(command -v python3)"
fi

rm -f "$TEST_RESULTS_JSON" "$TEST_REPORT_MD"
BENCH_OUT=$("$PYTHON_EXEC" "$BENCHMARK_SCRIPT" --quick --output "$TEST_RESULTS_JSON" --report "$TEST_REPORT_MD" 2>&1)
BENCH_RC=$?
if [ "$BENCH_RC" -eq 0 ] && echo "$BENCH_OUT" | grep -q "Benchmark complete!"; then
    pass "Benchmark execution completed successfully (exit code 0)"
else
    fail "Benchmark execution" "Failed with code $BENCH_RC: $BENCH_OUT"
fi

# Test 3: Validate structured JSON output schema and behavioral enforcement
info "Test 3: Validate structured JSON output schema and kernel enforcement metrics"
if [ -f "$TEST_RESULTS_JSON" ]; then
    JSON_EVAL=$("$PYTHON_EXEC" -c "
import sys, json

try:
    with open('$TEST_RESULTS_JSON') as f:
        d = json.load(f)
    
    # 1. System profile
    sys_p = d.get('system', {})
    has_sys = bool(sys_p.get('kernel') and sys_p.get('cpu_model') and sys_p.get('ram_total_gb', 0) > 0)
    
    # 2. Launch overhead
    e1 = d.get('launch_overhead', {})
    direct_med = e1.get('direct', {}).get('stats_ms', {}).get('median', 0)
    base_med = e1.get('minibox_baseline', {}).get('stats_ms', {}).get('median', 0)
    cg_med = e1.get('minibox_cgroups', {}).get('stats_ms', {}).get('median', 0)
    e1_fails = len(e1.get('direct', {}).get('failures', [])) + len(e1.get('minibox_cgroups', {}).get('failures', []))
    has_e1 = (direct_med > 0 and base_med > 0 and cg_med > 0 and e1_fails == 0)
    
    # 3. CPU throttling
    e2 = d.get('cpu_throttling', {})
    q50_pct = e2.get('minibox_quota_50pct', {}).get('stats_usage_pct', {}).get('median', 0)
    q25_pct = e2.get('minibox_quota_25pct', {}).get('stats_usage_pct', {}).get('median', 0)
    e2_fails = sum(len(e2[c].get('failures', [])) for c in e2)
    has_e2 = (45.0 <= q50_pct <= 55.0 and 20.0 <= q25_pct <= 30.0 and e2_fails == 0)
    
    # 4. Memory allocation vs enforcement
    e3 = d.get('memory', {})
    sub_perf = e3.get('performance_subceiling', {})
    mem_direct = sub_perf.get('direct', {}).get('stats_ms', {}).get('median', 0)
    mem_lim = sub_perf.get('minibox_limited_64M', {}).get('stats_ms', {}).get('median', 0)
    over_enf = e3.get('enforcement_overceiling', {})
    enf_confirmed = over_enf.get('enforcement_confirmed', False)
    all_sigkill = all(r.get('oom_killed_by_sigkill') and r.get('exit_code') == 137 for r in over_enf.get('runs', []))
    has_e3 = (mem_direct > 0 and mem_lim > 0 and enf_confirmed and all_sigkill)

    # 5. Fork spawning & PID limit semantics
    e4 = d.get('fork_spawning', {})
    p5 = e4.get('minibox_constrained_pids5', {})
    p5_verified = p5.get('semantics_verified', False) and p5.get('expected_exit_code') == 0
    p10 = e4.get('direct_10_forks', {})
    p10_verified = p10.get('semantics_verified', False) and p10.get('expected_exit_code') == 2
    has_e4 = (p5_verified and p10_verified)

    # 6. Lifecycle latency
    e5 = d.get('lifecycle_latency', {})
    has_e5 = (e5.get('start_latency_ms', {}).get('median', 0) > 0 and
              e5.get('stop_latency_ms', {}).get('median', 0) > 0 and
              len(e5.get('failures', [])) == 0)
    
    print(f'{has_sys} {has_e1} {has_e2} {has_e3} {has_e4} {has_e5} d={direct_med:.2f}ms cg={cg_med:.2f}ms q50={q50_pct:.1f}% q25={q25_pct:.1f}% oom={enf_confirmed} pids5={p5_verified}')
except Exception as e:
    print(f'ERROR: {e}')
")
    if echo "$JSON_EVAL" | grep -q "^True True True True True True"; then
        pass "JSON results valid and verified against kernel enforcement metrics: $JSON_EVAL"
    else
        fail "JSON schema/metrics" "Evaluation returned: $JSON_EVAL"
    fi
else
    fail "JSON output" "$TEST_RESULTS_JSON was not created"
fi

# Test 4: Validate Markdown evaluation report
info "Test 4: Validate Markdown performance report generation"
if [ -f "$TEST_REPORT_MD" ]; then
    if grep -q "MiniBox Performance Evaluation & Benchmark Report" "$TEST_REPORT_MD" && \
       grep -q "Experiment 1: Container Launch Overhead" "$TEST_REPORT_MD" && \
       grep -q "Experiment 2: Compute Workload & CFS Quota Accuracy" "$TEST_REPORT_MD" && \
       grep -q "Experiment 3: Memory Allocation Latency vs Limit Enforcement" "$TEST_REPORT_MD" && \
       grep -q "Experiment 4: Process Spawning & PID Limit Enforcement" "$TEST_REPORT_MD" && \
       grep -q "Experiment 5: Background Lifecycle & Supervision Latency" "$TEST_REPORT_MD"; then
        pass "Markdown report contains required experiment tables and conclusions"
    else
        fail "Markdown report" "Missing expected experiment sections in $TEST_REPORT_MD"
    fi
else
    fail "Markdown report" "$TEST_REPORT_MD was not created"
fi

# Test 5: Verify zero lingering container processes or cgroups
info "Test 5: Verify zero leftover workloads after benchmarking"
LEFTOVER=$("$CORE_TARGET" list --json)
WL_COUNT=$("$PYTHON_EXEC" -c "import sys, json; data = json.load(sys.stdin); print(len(data) if isinstance(data, list) else len(data.get('workloads', [])))" <<< "$LEFTOVER")
if [ "$WL_COUNT" -eq 0 ]; then
    pass "Zero leftover workloads after benchmark execution"
else
    fail "Leftover workloads" "$WL_COUNT workloads still present"
fi

# Clean up temporary test artifacts
rm -f "$TEST_RESULTS_JSON" "$TEST_REPORT_MD"

echo "=================================================="
echo "Phase 7 Test Results: $PASSED passed, $FAILED failed"
echo "=================================================="

if [ "$FAILED" -gt 0 ]; then
    exit 1
fi
exit 0

