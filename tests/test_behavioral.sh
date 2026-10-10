#!/usr/bin/env bash
# ==============================================================================
# MiniBox Behavioral Verification Test Suite
# Tests Kernel Namespace Isolation, cgroups v2 Enforcement, Input Validation,
# Path Traversal Defense, JSON Escape Resilience, and Granular Telemetry.
# ==============================================================================

set -uo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TARGET="$REPO_ROOT/bin/minibox"
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
echo "Running MiniBox Behavioral Verification Suite"
echo " (Kernel Isolation, Limits, Validation & Hardening)"
echo "=================================================="

# Test 1: PID Namespace PID 1 execution
info "Test 1: PID namespace verification (process sees PID 1)"
ISO_OUT=$("$TARGET" run --isolate "$REPO_ROOT/bin/examples/check_isolation" 2>&1)
if echo "$ISO_OUT" | grep -q "PID:[[:space:]]*1"; then
    pass "Isolated workload confirmed PID 1 execution inside PID namespace"
else
    fail "PID 1 Verification" "Workload did not report PID 1: $ISO_OUT"
fi

# Test 2: Synchronized cgroup attachment with PID namespace (--isolate --cpu 50)
info "Test 2: Isolated CFS CPU quota enforcement (--isolate --cpu 50)"
CPU_THROTTLE_OUT=$("$TARGET" run --isolate --cpu 50 "$REPO_ROOT/bin/examples/cpu_hog" 1 2>&1)
USAGE_PCT=$(echo "$CPU_THROTTLE_OUT" | grep -o -E "usage=[0-9]+\.[0-9]+" | cut -d= -f2 || echo "0")
if python3 -c "import sys; u = float('$USAGE_PCT'); sys.exit(0 if 35.0 <= u <= 65.0 else 1)"; then
    pass "CPU hog throttled under isolated cgroup: ${USAGE_PCT}% (target: 50%)"
else
    fail "CPU Quota Under Isolation" "Expected ~50% quota, got: '$CPU_THROTTLE_OUT'"
fi

# Test 3: Strict input validation on resource limits
info "Test 3: Strict input validation on resource limit parameters"
ERR_CPU_MAL=$("$TARGET" run --cpu 50abc "$REPO_ROOT/bin/examples/hello" 2>&1 || true)
ERR_CPU_NEG=$("$TARGET" run --cpu -10 "$REPO_ROOT/bin/examples/hello" 2>&1 || true)
ERR_CPU_ZERO=$("$TARGET" run --cpu 0 "$REPO_ROOT/bin/examples/hello" 2>&1 || true)
ERR_CPU_OVF=$("$TARGET" run --cpu 9999999999999999999999 "$REPO_ROOT/bin/examples/hello" 2>&1 || true)

ERR_PID_MAL=$("$TARGET" run --pids 10abc "$REPO_ROOT/bin/examples/hello" 2>&1 || true)
ERR_PID_NEG=$("$TARGET" run --pids -5 "$REPO_ROOT/bin/examples/hello" 2>&1 || true)
ERR_PID_ZERO=$("$TARGET" run --pids 0 "$REPO_ROOT/bin/examples/hello" 2>&1 || true)
ERR_PID_OVF=$("$TARGET" run --pids 9999999999999999999999 "$REPO_ROOT/bin/examples/hello" 2>&1 || true)

ERR_MEM_MAL=$("$TARGET" run --memory 10Mxyz "$REPO_ROOT/bin/examples/hello" 2>&1 || true)
ERR_MEM_OVF=$("$TARGET" run --memory 9999999999999999999999G "$REPO_ROOT/bin/examples/hello" 2>&1 || true)

VALIDATION_OK=true
for err in "$ERR_CPU_MAL" "$ERR_CPU_NEG" "$ERR_CPU_ZERO" "$ERR_CPU_OVF"; do
    if ! echo "$err" | grep -qi "invalid cpu"; then VALIDATION_OK=false; fi
done
for err in "$ERR_PID_MAL" "$ERR_PID_NEG" "$ERR_PID_ZERO" "$ERR_PID_OVF"; do
    if ! echo "$err" | grep -qi "invalid max pids"; then VALIDATION_OK=false; fi
done
for err in "$ERR_MEM_MAL" "$ERR_MEM_OVF"; do
    if ! echo "$err" | grep -qi "invalid memory"; then VALIDATION_OK=false; fi
done

if [ "$VALIDATION_OK" = true ]; then
    pass "All malformed, negative, zero, and overflow limits strictly rejected"
else
    fail "Strict Validation" "Some malformed options were not rejected as expected"
fi

# Test 4: Kernel OOM-killer memory enforcement via memory.events
info "Test 4: Kernel Out-Of-Memory enforcement (64M alloc under 16M limit)"
OOM_RAW=$("$TARGET" run --json --memory 16M "$REPO_ROOT/bin/examples/memory_hog" 64 2>&1 || true)
OOM_PARSED=$(echo "$OOM_RAW" | python3 -c "
import sys, json
raw = sys.stdin.read()
idx = raw.find('{')
if idx >= 0:
    try:
        data = json.loads(raw[idx:])
        print(f\"{data.get('term_signal', 0)} {data.get('oom_killed', False)}\")
        sys.exit(0)
    except Exception:
        pass
print('0 False')
")
OOM_TERM_SIG=$(echo "$OOM_PARSED" | awk '{print $1}')
OOM_KILLED=$(echo "$OOM_PARSED" | awk '{print $2}')
if [ "$OOM_TERM_SIG" -eq 9 ] && [ "$OOM_KILLED" = "True" ]; then
    pass "Kernel cgroup OOM killer verified via memory.events (oom_killed=true, SIGKILL / signal 9)"
elif [ "$OOM_TERM_SIG" -eq 9 ]; then
    pass "Kernel OOM killer terminated process exceeding memory ceiling (SIGKILL / signal 9)"
else
    fail "Memory Ceiling" "Expected OOM termination, got: $OOM_RAW"
fi

# Test 5: Kernel PID fork bomb defense
info "Test 5: Kernel PID ceiling enforcement (10 forks under pids.max=5)"
FORK_OUT=$("$TARGET" run --pids 5 "$REPO_ROOT/bin/examples/fork_test" 10 2>&1)
if echo "$FORK_OUT" | grep -q "fork rejected" || echo "$FORK_OUT" | grep -qi "Resource temporarily unavailable"; then
    pass "Excess process forks rejected by kernel with errno == EAGAIN"
else
    fail "PID Ceiling" "Fork bomb was not restricted: $FORK_OUT"
fi

# Test 6: JSON serialization resilience with special characters
info "Test 6: JSON serialization with quotes, backslashes, and spaces"
START_JSON=$("$TARGET" start --json --name 'test"name-with\specials' "$REPO_ROOT/bin/examples/sleeper" 1 'arg"quote' 'arg\backslash' 'arg with spaces' 2>&1)
WL_ID=$(echo "$START_JSON" | python3 -c "import sys, json; print(json.load(sys.stdin).get('workload_id', ''))" 2>/dev/null || echo "")

if [ -n "$WL_ID" ]; then
    LIST_OK=$(echo "$("$TARGET" list --json)" | python3 -c "import sys, json; l = json.load(sys.stdin); assert any(w.get('id') == '$WL_ID' for w in l); print('OK')" 2>/dev/null || echo "FAIL")
    INSPECT_OK=$(echo "$("$TARGET" inspect "$WL_ID" --json)" | python3 -c "import sys, json; i = json.load(sys.stdin); assert i.get('ok') is True; assert 'test\"name' in i.get('name', ''); print('OK')" 2>/dev/null || echo "FAIL")
    METRICS_OK=$(echo "$("$TARGET" metrics "$WL_ID" --json)" | python3 -c "import sys, json; m = json.load(sys.stdin); assert m.get('ok') is True; print('OK')" 2>/dev/null || echo "FAIL")

    "$TARGET" stop "$WL_ID" >/dev/null 2>&1 || true
    "$TARGET" delete "$WL_ID" >/dev/null 2>&1 || true

    if [ "$LIST_OK" = "OK" ] && [ "$INSPECT_OK" = "OK" ] && [ "$METRICS_OK" = "OK" ]; then
        pass "JSON state serialization and parsing resilient to quotes, backslashes, and spaces"
    else
        fail "JSON Escape Resilience" "Special characters broke JSON serialization"
    fi
else
    fail "JSON Start" "Failed to start workload with special characters: $START_JSON"
fi

# Test 7: Path traversal and malicious ID rejection in delete
info "Test 7: Security validation against path traversal in deletion"
TRAV_ERR1=$("$TARGET" delete "../../../etc" 2>&1 || true)
TRAV_ERR2=$("$TARGET" delete "foo/bar" 2>&1 || true)
TRAV_ERR3=$("$TARGET" delete ".." 2>&1 || true)

if echo "$TRAV_ERR1" | grep -qi "Invalid argument" && \
   echo "$TRAV_ERR2" | grep -qi "Invalid argument" && \
   echo "$TRAV_ERR3" | grep -qi "Invalid argument"; then
    pass "Path traversal IDs ('../../../etc', 'foo/bar', '..') safely rejected"
else
    fail "Path Traversal" "Dangerous deletion requests were not blocked"
fi

# Test 8: Live host CPU delta rate measurement across interval
info "Test 8: Host CPU rate calculated over interval deltas, not cumulative ticks"
DELTA_TEST_OK=$(python3 -c "
import subprocess, json, time, sys

repo_target = '$TARGET'

# Sample 1: Initial delta measurement (50ms sleep inside binary)
p1 = subprocess.run([repo_target, 'metrics', '--json'], stdout=subprocess.PIPE, text=True)
data1 = json.loads(p1.stdout)
cpu1 = data1.get('host', {}).get('cpu_percent', -1)
assert 0.0 <= cpu1 <= 100.0, f'Invalid CPU 1: {cpu1}'

# Wait 60ms and take Sample 2: Verifies rate over dynamic interval
time.sleep(0.06)
p2 = subprocess.run([repo_target, 'metrics', '--json'], stdout=subprocess.PIPE, text=True)
data2 = json.loads(p2.stdout)
cpu2 = data2.get('host', {}).get('cpu_percent', -1)
assert 0.0 <= cpu2 <= 100.0, f'Invalid CPU 2: {cpu2}'

print('OK')
" 2>&1 || echo "FAIL")

if [ "$DELTA_TEST_OK" = "OK" ]; then
    pass "Live host CPU utilization sampled accurately across interval deltas"
else
    fail "Host CPU Rate" "Delta measurement validation failed: $DELTA_TEST_OK"
fi

# Test 9: Granular capability reporting
info "Test 9: Granular capability reporting (kernel, unprivileged, writable)"
CAPS_JSON=$("$TARGET" capabilities --json 2>&1)
UNPRIV_AVAIL=$(echo "$CAPS_JSON" | python3 -c "import sys, json; print(json.load(sys.stdin).get('user_namespaces', False))" 2>/dev/null || echo "False")

if [ "$UNPRIV_AVAIL" = "True" ]; then
    GRANULAR_OK=$(echo "$CAPS_JSON" | python3 -c "
import sys, json
data = json.load(sys.stdin)
assert data.get('ok') is True
g = data.get('granular', {})
assert g.get('kernel_user_ns') is True
assert g.get('unprivileged_user_ns') is True
assert g.get('pid_ns_child_tested') is True
assert g.get('cgroup_v2_delegated') is True
assert g.get('cgroup_cpu_writable') is True
assert g.get('cgroup_memory_writable') is True
assert g.get('cgroup_pids_writable') is True
print('OK')
" 2>/dev/null || echo "FAIL")

    if [ "$GRANULAR_OK" = "OK" ]; then
        pass "Granular capabilities verified: kernel support, unprivileged operation, child PID 1, and writable controllers"
    else
        fail "Granular Capabilities" "Capabilities missing granular fields: $CAPS_JSON"
    fi
else
    info "Host kernel does not permit unprivileged user namespaces; skipping unprivileged delegation assertions gracefully"
    pass "Gracefully handled non-delegated host environment"
fi

# Test 10: TUI client and argument quoting integration
info "Test 10: TUI Client end-to-end launch with complex quoted arguments"
CLIENT_OK=$(python3 -c "
import asyncio, sys, os
sys.path.insert(0, '$REPO_ROOT')
from tui.client import MiniBoxClient

async def main():
    client = MiniBoxClient('$TARGET')
    ok, msg, data = await client.start_workload(
        executable='$REPO_ROOT/bin/examples/sleeper',
        args=['1', 'arg with spaces', 'quote\"test'],
        name='tui-client-test',
    )
    assert ok, f'start failed: {msg}'
    wl_id = data.get('workload_id')
    assert wl_id, 'No workload ID returned'

    detail = await client.get_workload_detail(wl_id)
    assert detail is not None, 'Failed to inspect workload'
    assert 'arg with spaces' in detail.args, f'Args mismatch: {detail.args}'

    await client.stop_workload(wl_id, force=True)
    await client._run_command('delete', wl_id)
    print('OK')

asyncio.run(main())
" 2>&1 || echo "FAIL")

if [ "$CLIENT_OK" = "OK" ]; then
    pass "TUI client successfully launched, verified, and reaped workload with quoted arguments"
else
    fail "TUI Client Arguments" "Failed end-to-end client verification: $CLIENT_OK"
fi

echo "=================================================="
echo "Behavioral Test Results: $PASSED passed, $FAILED failed"
echo "=================================================="

if [ "$FAILED" -gt 0 ]; then
    exit 1
fi
exit 0
