#!/usr/bin/env bash
# MiniBox Phase 4 Automated Test Suite
# Tests resource controls via Linux cgroups v2

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
echo "Running MiniBox Phase 4 Test Suite"
echo "=================================================="

# Test 1: Version reports Phase 4
info "Test 1: Version reports Phase 4"
VER_OUT=$("$TARGET" version)
if echo "$VER_OUT" | grep -E -q "0\.[4-9]\.[0-9]-phase"; then
    pass "Version string contains expected version ($VER_OUT)"
else
    fail "Version string" "Got '$VER_OUT'"
fi

# Test 2: Capabilities detects cgroups v2
info "Test 2: Capabilities detects cgroups v2"
CAPS_JSON=$("$TARGET" capabilities --json)
if echo "$CAPS_JSON" | grep -q '"cgroups_v2": true'; then
    pass "Capabilities reports cgroups_v2 as supported"
elif echo "$CAPS_JSON" | grep -q '"ok": true'; then
    pass "Capabilities report validly indicates cgroups_v2 is unavailable in this environment"
else
    fail "Capabilities" "Invalid capabilities JSON output: $CAPS_JSON"
fi

# Test 3: Invalid limit options rejected
info "Test 3: Invalid limit options rejected"
ERR_CPU=$("$TARGET" run --cpu 0 ./bin/examples/hello 2>&1 || true)
if echo "$ERR_CPU" | grep -qi "invalid cpu"; then
    pass "Invalid CPU percentage rejected"
else
    fail "Invalid CPU" "Expected rejection error, got '$ERR_CPU'"
fi

ERR_MEM=$("$TARGET" run --memory notanumber ./bin/examples/hello 2>&1 || true)
if echo "$ERR_MEM" | grep -qi "invalid memory"; then
    pass "Invalid memory size rejected"
else
    fail "Invalid memory" "Expected rejection error, got '$ERR_MEM'"
fi

ERR_PIDS=$("$TARGET" run --pids -2 ./bin/examples/hello 2>&1 || true)
if echo "$ERR_PIDS" | grep -qi "invalid max pids"; then
    pass "Invalid pids max rejected"
else
    fail "Invalid pids" "Expected rejection error, got '$ERR_PIDS'"
fi

# Test 4: Resource limits reflected in inspect JSON
info "Test 4: Resource limits reflected in inspect JSON"
RUN_JSON=$("$TARGET" run --json --id "test-phase4-limits" --pids 10 --memory 64M --cpu 40 ./bin/examples/hello 2>/dev/null)
INSPECT_JSON=$("$TARGET" inspect --json test-phase4-limits)
if echo "$INSPECT_JSON" | grep -q '"pids_max": 10' && \
   echo "$INSPECT_JSON" | grep -q '"memory_max_bytes": 67108864' && \
   echo "$INSPECT_JSON" | grep -q '"cpu_percent": 40'; then
    pass "Configured limits properly saved and inspected"
else
    fail "Inspect limits" "Limits not found in inspect output: $INSPECT_JSON"
fi

# Test 5: pids.max kernel enforcement (fork_test fails with EAGAIN)
info "Test 5: pids.max kernel enforcement"
# Without limit, fork_test spawns all 10 forks and exits with 2
NO_LIMIT_RC=0
"$TARGET" run ./bin/examples/fork_test 10 >/dev/null 2>&1 || NO_LIMIT_RC=$?
if [ "$NO_LIMIT_RC" -eq 2 ]; then
    pass "Unconstrained fork_test succeeds with code 2"
else
    fail "Unconstrained fork_test" "Expected exit code 2, got $NO_LIMIT_RC"
fi

# With --pids 4, fork_test hits limit, prints FORK_LIMIT_REACHED, and exits with 0
PIDS_OUT=$("$TARGET" run --pids 4 ./bin/examples/fork_test 10 2>&1)
PIDS_RC=$?
if [ "$PIDS_RC" -eq 0 ] && echo "$PIDS_OUT" | grep -q "FORK_LIMIT_REACHED"; then
    pass "pids.max kernel enforcement verified (EAGAIN encountered)"
else
    fail "pids.max enforcement" "Expected exit 0 with FORK_LIMIT_REACHED, got rc=$PIDS_RC, out='$PIDS_OUT'"
fi

# Test 6: memory.max kernel enforcement (memory_hog killed by OOM killer)
info "Test 6: memory.max kernel enforcement"
MEM_OUT=$("$TARGET" run --memory 16M ./bin/examples/memory_hog 64 2>&1 || true)
if echo "$MEM_OUT" | grep -q "Terminated by signal: 9"; then
    pass "memory.max kernel enforcement verified (OOM killed by SIGKILL)"
else
    fail "memory.max enforcement" "Expected Terminated by signal: 9, got '$MEM_OUT'"
fi

# Test 7: cpu.max CFS quota bandwidth throttling
info "Test 7: cpu.max CFS bandwidth throttling"
CPU_OUT=$("$TARGET" run --cpu 50 ./bin/examples/cpu_hog 1 2>&1)
if echo "$CPU_OUT" | grep -q "CPU_HOG_DONE"; then
    USAGE_PCT=$(echo "$CPU_OUT" | grep "CPU_HOG_DONE" | sed -E 's/.*usage=([0-9]+).*/\1/')
    if [ -n "$USAGE_PCT" ] && [ "$USAGE_PCT" -ge 40 ] && [ "$USAGE_PCT" -le 60 ]; then
        pass "cpu.max quota enforced around 50% (measured $USAGE_PCT%)"
    else
        fail "cpu.max quota" "Expected usage around 50%, measured '$USAGE_PCT%'"
    fi
else
    fail "cpu.max run" "CPU hog did not complete: '$CPU_OUT'"
fi

# Test 8: Combined isolation + resource limits
info "Test 8: Combined isolation and resource limits"
COMBINED_OUT=$("$TARGET" run --isolate --pids 5 --memory 64M ./bin/examples/check_isolation 2>&1)
COMBINED_RC=$?
if [ "$COMBINED_RC" -eq 0 ] && echo "$COMBINED_OUT" | grep -q "PID:      1"; then
    pass "Combined isolation namespaces + cgroup resource limits succeeded"
else
    fail "Combined isolation + limits" "Failed with rc=$COMBINED_RC, out='$COMBINED_OUT'"
fi

# Test 9: Background workload resource limits and cgroup attachment
info "Test 9: Background workload resource limits"
BG_OUT=$("$TARGET" start --pids 5 --memory 32M --cpu 50 ./bin/examples/sleeper 2)
BG_ID=$(echo "$BG_OUT" | grep -o 'wl-[0-9a-f]*' | head -n1)
if [ -n "$BG_ID" ]; then
    INSPECT_BG=$("$TARGET" inspect --json "$BG_ID")
    if echo "$INSPECT_BG" | grep -q '"cgroup_active": true' && \
       echo "$INSPECT_BG" | grep -q '"pids_max": 5'; then
        pass "Background workload active in cgroup with limits"
    else
        fail "Background cgroup" "cgroup metadata missing in inspect: '$INSPECT_BG'"
    fi
else
    fail "Background start" "Failed to get workload ID: '$BG_OUT'"
fi

# Test 10: Automatic cgroup directory cleanup on completion
info "Test 10: Automatic cgroup cleanup on completion"
# Wait for sleeper from Test 9 to finish
sleep 2.5
CG_PATH=$(python3 -c "
import json, subprocess
out = subprocess.check_output(['$TARGET', 'inspect', '--json', '$BG_ID'])
print(json.loads(out.decode())['cgroup_path'])
")
if [ -n "$CG_PATH" ] && [ ! -d "$CG_PATH" ]; then
    pass "Workload cgroup directory cleanly removed on workload exit"
else
    fail "Cgroup cleanup" "Cgroup directory still exists at $CG_PATH"
fi

# Test 11: Controlled stop cleans up cgroup
info "Test 11: Controlled stop cleans up cgroup"
STOP_OUT=$("$TARGET" start --pids 5 ./bin/examples/sleeper 10)
STOP_ID=$(echo "$STOP_OUT" | grep -o 'wl-[0-9a-f]*' | head -n1)
STOP_CG=$(python3 -c "
import json, subprocess
out = subprocess.check_output(['$TARGET', 'inspect', '--json', '$STOP_ID'])
print(json.loads(out.decode())['cgroup_path'])
")
"$TARGET" stop "$STOP_ID" >/dev/null 2>&1
sleep 0.5
if [ -n "$STOP_CG" ] && [ ! -d "$STOP_CG" ]; then
    pass "Stopped workload cgroup directory cleanly removed"
else
    fail "Stop cgroup cleanup" "Cgroup directory still exists at $STOP_CG"
fi

echo "=================================================="
echo "Phase 4 Test Results: $PASSED passed, $FAILED failed"
echo "=================================================="

if [ "$FAILED" -gt 0 ]; then
    exit 1
fi
exit 0
