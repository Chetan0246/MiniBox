#!/usr/bin/env bash
# MiniBox Phase 5 Automated Test Suite
# Tests resource monitoring and metrics collection

set -uo pipefail

TARGET="./bin/minibox"
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
    "$TARGET" cleanup >/dev/null 2>&1 || true
}
trap cleanup EXIT

echo "=================================================="
echo "Running MiniBox Phase 5 Test Suite"
echo " (Resource Monitoring & Metrics Collection)"
echo "=================================================="

# Test 1: Version reports Phase 5
info "Test 1: Version reports Phase 5"
VER_OUT=$("$TARGET" version)
if echo "$VER_OUT" | grep -q "0.5.0-phase5"; then
    pass "Version string contains 0.5.0-phase5"
else
    fail "Version string" "Got '$VER_OUT'"
fi

# Test 2: Host resource monitoring (table and JSON)
info "Test 2: Host resource metrics collection"
HOST_OUT=$("$TARGET" metrics)
if echo "$HOST_OUT" | grep -q "Host Resource Overview:" && \
   echo "$HOST_OUT" | grep -q "Memory Used:" && \
   echo "$HOST_OUT" | grep -q "Load Averages:"; then
    pass "Host metrics table output contains required headers and fields"
else
    fail "Host metrics table" "Missing expected text in: $HOST_OUT"
fi

HOST_JSON=$("$TARGET" metrics --json)
HOST_EVAL=$(python3 -c "
import sys, json
try:
    d = json.load(sys.stdin)
    h = d.get('host', {})
    valid = h.get('valid') == True
    mem_total = h.get('mem_total_bytes', 0) > 0
    mem_avail = h.get('mem_avail_bytes', 0) > 0
    mem_used = h.get('mem_used_bytes', 0) > 0
    load_ok = h.get('load_1m', -1) >= 0 and h.get('load_5m', -1) >= 0
    print(f'{valid} {mem_total} {mem_avail} {mem_used} {load_ok}')
except Exception as e:
    print(f'ERROR: {e}')
" <<< "$HOST_JSON")

if [ "$HOST_EVAL" = "True True True True True" ]; then
    pass "Host JSON metrics valid and non-zero: $HOST_EVAL"
else
    fail "Host JSON metrics" "Evaluation returned: $HOST_EVAL"
fi

# Test 3: Command alias 'stats' functions identically
info "Test 3: Alias 'minibox stats' command"
STATS_OUT=$("$TARGET" stats)
if echo "$STATS_OUT" | grep -q "Host Resource Overview:"; then
    pass "'minibox stats' alias is recognized and outputs host resource overview"
else
    fail "minibox stats alias" "Output was: $STATS_OUT"
fi

# Test 4: Error handling for nonexistent workload ID
info "Test 4: Nonexistent workload handling"
ERR_CODE=0
ERR_OUT=$("$TARGET" metrics nonexistent-wl-99999 2>&1) || ERR_CODE=$?
if [ "$ERR_CODE" -ne 0 ] && echo "$ERR_OUT" | grep -qi "not found"; then
    pass "Nonexistent workload returned non-zero exit code ($ERR_CODE) with error message"
else
    fail "Nonexistent workload" "Code=$ERR_CODE, Out=$ERR_OUT"
fi

ERR_JSON=$("$TARGET" metrics --json nonexistent-wl-99999 2>&1) || true
if echo "$ERR_JSON" | grep -q '"ok": false'; then
    pass "Nonexistent workload returns JSON error object with 'ok': false"
else
    fail "Nonexistent workload JSON" "Got: $ERR_JSON"
fi

# Test 5: Live background workload metrics (sleeper)
info "Test 5: Live background workload metrics"
START_JSON=$("$TARGET" start --json --name test-live-sleeper ./bin/examples/sleeper 15)
BG_ID=$(python3 -c "import sys, json; print(json.load(sys.stdin)['workload_id'])" <<< "$START_JSON")

sleep 0.5
LIVE_JSON=$("$TARGET" metrics --json "$BG_ID")
LIVE_EVAL=$(python3 -c "
import sys, json
try:
    d = json.load(sys.stdin)
    ok = d.get('ok') == True
    active = d.get('active') == True
    state = d.get('state') == 'RUNNING'
    pids_ok = d.get('pids', {}).get('current', 0) >= 1
    mem_ok = d.get('memory', {}).get('current_bytes', 0) > 0
    print(f'{ok} {active} {state} {pids_ok} {mem_ok}')
except Exception as e:
    print(f'ERROR: {e}')
" <<< "$LIVE_JSON")

if [ "$LIVE_EVAL" = "True True True True True" ]; then
    pass "Live background workload reports active state, valid PID count, and resident memory"
else
    fail "Live workload metrics" "Evaluation returned: $LIVE_EVAL"
fi

# Test 6: Active memory allocation metrics (memory_hog)
info "Test 6: Active memory allocation metrics"
MEM_START_JSON=$("$TARGET" start --json --name test-live-mem --memory 64M ./bin/examples/memory_hog 25 5)
MEM_ID=$(python3 -c "import sys, json; print(json.load(sys.stdin)['workload_id'])" <<< "$MEM_START_JSON")

# Allow memory_hog to allocate 25MB (25 * 10ms = 250ms)
sleep 0.8
MEM_JSON=$("$TARGET" metrics --json "$MEM_ID")
MEM_EVAL=$(python3 -c "
import sys, json
try:
    d = json.load(sys.stdin)
    mem = d.get('memory', {})
    cur_bytes = mem.get('current_bytes', 0)
    rss_bytes = mem.get('rss_bytes', 0)
    peak_bytes = mem.get('peak_bytes', 0)
    lim_bytes = mem.get('limit_bytes', 0)
    # Target was 25 MB (~26,214,400 bytes), verify at least 20 MB detected
    has_target = (cur_bytes >= 20000000 or rss_bytes >= 20000000)
    limit_ok = (lim_bytes == 67108864)
    print(f'{has_target} {limit_ok} {cur_bytes} {peak_bytes}')
except Exception as e:
    print(f'ERROR: {e}')
" <<< "$MEM_JSON")

if echo "$MEM_EVAL" | grep -q "^True True"; then
    pass "Active memory metrics detected genuine allocated memory: $MEM_EVAL"
else
    fail "Active memory metrics" "Evaluation returned: $MEM_EVAL"
fi
"$TARGET" stop "$MEM_ID" >/dev/null 2>&1 || true

# Test 7: CFS CPU throttling metrics collection
info "Test 7: CFS CPU throttling metrics collection"
CPU_START_JSON=$("$TARGET" start --json --name test-live-cpu --cpu 40 ./bin/examples/cpu_hog 8)
CPU_ID=$(python3 -c "import sys, json; print(json.load(sys.stdin)['workload_id'])" <<< "$CPU_START_JSON")

# Allow cpu_hog to burn CPU and trigger throttling
sleep 1.5
CPU_JSON=$("$TARGET" metrics --json --sample-ms 200 "$CPU_ID")
CPU_EVAL=$(python3 -c "
import sys, json
try:
    d = json.load(sys.stdin)
    cpu = d.get('cpu', {})
    valid = cpu.get('valid') == True
    pct = cpu.get('usage_percent', 0.0)
    nr_throttled = cpu.get('nr_throttled', 0)
    throttled_us = cpu.get('throttled_usec', 0)
    throttled_ok = (nr_throttled > 0 and throttled_us > 0)
    pct_ok = (pct > 0.0)
    print(f'{valid} {throttled_ok} {pct_ok} pct={pct:.1f}% throttled={nr_throttled} periods ({throttled_us}us)')
except Exception as e:
    print(f'ERROR: {e}')
" <<< "$CPU_JSON")

if echo "$CPU_EVAL" | grep -q "^True True True"; then
    pass "CFS bandwidth throttling metrics captured: $CPU_EVAL"
else
    fail "CPU throttling metrics" "Evaluation returned: $CPU_EVAL"
fi

# Also test human-readable metrics output for CFS throttle line
CPU_HUMAN=$("$TARGET" metrics "$CPU_ID")
if echo "$CPU_HUMAN" | grep -q "CFS Throttle:"; then
    pass "Human-readable workload metrics contains CFS Throttle summary line"
else
    fail "Human-readable CFS Throttle" "Line missing in: $CPU_HUMAN"
fi
"$TARGET" stop "$CPU_ID" >/dev/null 2>&1 || true

# Test 8: Instantaneous CPU rate sampling (--sample-ms)
info "Test 8: Instantaneous CPU rate sampling"
SAMPLE_START_JSON=$("$TARGET" start --json --name test-sample-cpu ./bin/examples/cpu_hog 5)
SAMPLE_ID=$(python3 -c "import sys, json; print(json.load(sys.stdin)['workload_id'])" <<< "$SAMPLE_START_JSON")

sleep 0.5
SAMPLE_JSON=$("$TARGET" metrics --json --sample-ms 150 "$SAMPLE_ID")
SAMPLE_PCT=$(python3 -c "import sys, json; print(json.load(sys.stdin).get('cpu', {}).get('usage_percent', 0.0))" <<< "$SAMPLE_JSON")
SAMPLE_VALID=$(python3 -c "import sys, json; print(float('$SAMPLE_PCT') > 0.0)")

if [ "$SAMPLE_VALID" = "True" ]; then
    pass "Sampled instantaneous CPU rate measured active utilization: ${SAMPLE_PCT}%"
else
    fail "Sampled CPU rate" "Expected > 0.0%, got ${SAMPLE_PCT}%"
fi
"$TARGET" stop "$SAMPLE_ID" >/dev/null 2>&1 || true

# Test 9: Exited workload behavior (metrics unavailable, not invented)
info "Test 9: Exited workload metrics handling"
"$TARGET" stop "$BG_ID" >/dev/null 2>&1 || true
sleep 0.5

EXITED_JSON=$("$TARGET" metrics --json "$BG_ID")
EXITED_EVAL=$(python3 -c "
import sys, json
try:
    d = json.load(sys.stdin)
    active = d.get('active')
    cpu_valid = d.get('cpu', {}).get('valid')
    mem_valid = d.get('memory', {}).get('valid')
    pids_valid = d.get('pids', {}).get('valid')
    elapsed_ms = d.get('elapsed_ms', 0) > 0
    print(f'{active} {cpu_valid} {mem_valid} {pids_valid} {elapsed_ms}')
except Exception as e:
    print(f'ERROR: {e}')
" <<< "$EXITED_JSON")

if [ "$EXITED_EVAL" = "False False False False True" ]; then
    pass "Exited workload correctly marks dynamic metrics as invalid/unavailable while preserving elapsed time"
else
    fail "Exited workload metrics" "Got: $EXITED_EVAL"
fi

EXITED_HUMAN=$("$TARGET" metrics "$BG_ID")
if echo "$EXITED_HUMAN" | grep -q "CPU Metrics:    unavailable" && \
   echo "$EXITED_HUMAN" | grep -q "Memory Metrics: unavailable"; then
    pass "Exited workload human output explicitly states metrics are unavailable"
else
    fail "Exited workload human output" "Got: $EXITED_HUMAN"
fi

# Test 10: Multi-workload overview table displays '-' for inactive metrics
info "Test 10: Workload list table displays '-' for exited dynamic metrics"
TABLE_OUT=$("$TARGET" metrics)
if echo "$TABLE_OUT" | grep "$BG_ID" | grep -q -- "- *- *-"; then
    pass "Overview table shows '-' placeholders rather than invented values for exited workload"
else
    fail "Overview table placeholders" "Table row did not match: $(echo "$TABLE_OUT" | grep "$BG_ID")"
fi

# Test 11: JSON strict validation
info "Test 11: JSON strict parsing validation"
HOST_LINT=$(echo "$HOST_JSON" | python3 -m json.tool >/dev/null 2>&1 && echo "OK" || echo "FAIL")
WL_LINT=$(echo "$EXITED_JSON" | python3 -m json.tool >/dev/null 2>&1 && echo "OK" || echo "FAIL")
if [ "$HOST_LINT" = "OK" ] && [ "$WL_LINT" = "OK" ]; then
    pass "All JSON outputs pass strict JSON syntax parsing"
else
    fail "JSON syntax" "Host lint: $HOST_LINT, Workload lint: $WL_LINT"
fi

# Test 12: Workload cleanup
info "Test 12: Workload cleanup"
CLEAN_JSON=$("$TARGET" cleanup --json)
CLEAN_OK=$(python3 -c "import sys, json; print(json.load(sys.stdin).get('ok'))" <<< "$CLEAN_JSON")
if [ "$CLEAN_OK" = "True" ]; then
    pass "All test workloads successfully cleaned up"
else
    fail "Cleanup" "Result: $CLEAN_JSON"
fi

echo "=================================================="
echo "Phase 5 Test Results: $PASSED passed, $FAILED failed"
echo "=================================================="

if [ "$FAILED" -gt 0 ]; then
    exit 1
fi
exit 0
