#!/usr/bin/env bash
set -euo pipefail

# Use isolated state directory for testing
export MINIBOX_STATE_DIR="$(mktemp -d /tmp/minibox-test-p3-XXXXXX)/workloads"
mkdir -p "$MINIBOX_STATE_DIR"

cleanup_test_dir() {
    rm -rf "$(dirname "$MINIBOX_STATE_DIR")"
}
trap cleanup_test_dir EXIT

MINIBOX="./bin/minibox"
FAILED=0
TOTAL=0

pass() {
    echo "  [PASS] $1"
}

fail() {
    echo "  [FAIL] $1: $2"
    FAILED=$((FAILED + 1))
}

run_test() {
    TOTAL=$((TOTAL + 1))
    echo "Test $TOTAL: $1"
}

if [[ ! -x "$MINIBOX" ]]; then
    echo "Error: $MINIBOX not found or not executable. Run 'make' first."
    exit 1
fi

HOST_HOSTNAME=$(hostname)

echo "=========================================="
echo " MiniBox Phase 3 Test Suite"
echo " (Linux Namespace Isolation)"
echo "=========================================="

# Test 1: Capability detection command (table and json)
run_test "Linux capability detection (minibox capabilities)"
CAPS_TABLE=$("$MINIBOX" capabilities)
CAPS_JSON=$("$MINIBOX" capabilities --json)
USER_NS_OK=$(echo "$CAPS_JSON" | python3 -c "import sys, json; d = json.load(sys.stdin); print(d.get('ok'), d.get('user_namespaces'), d.get('uts_namespace'))")

if [[ "$CAPS_TABLE" == *"MiniBox Linux Capability Report"* && "$USER_NS_OK" == "True True True" ]]; then
    pass "Reported available capabilities accurately in table and JSON"
else
    fail "Capability detection" "Table: $CAPS_TABLE, JSON: $CAPS_JSON"
fi

# Test 2: Unisolated baseline run (default path untouched)
run_test "Unisolated execution retains host configuration"
BASELINE_OUT=$("$MINIBOX" run bin/examples/check_isolation)
if [[ "$BASELINE_OUT" == *"Hostname: $HOST_HOSTNAME"* ]] && ! echo "$BASELINE_OUT" | grep -q -E "PID:[[:space:]]+1$"; then
    pass "Baseline workload saw host hostname ($HOST_HOSTNAME) and non-PID-1 host PID"
else
    fail "Baseline execution" "Output: $BASELINE_OUT"
fi

# Test 3: UTS namespace isolation (foreground)
run_test "UTS namespace isolation sets container hostname without affecting host"
UTS_OUT=$("$MINIBOX" run --uts --hostname test-box-42 bin/examples/check_isolation)
CURRENT_HOST_HOSTNAME=$(hostname)

if [[ "$UTS_OUT" == *"Hostname: test-box-42"* && "$CURRENT_HOST_HOSTNAME" == "$HOST_HOSTNAME" ]]; then
    pass "Container saw isolated hostname 'test-box-42'; host hostname remains '$HOST_HOSTNAME'"
else
    fail "UTS namespace isolation" "Container saw: $UTS_OUT, Host saw: $CURRENT_HOST_HOSTNAME"
fi

# Test 4: UTS namespace in background workload
run_test "UTS namespace in background asynchronous workload"
BG_START=$("$MINIBOX" start --uts --hostname bg-isolated-box --json bin/examples/check_isolation)
BG_ID=$(echo "$BG_START" | python3 -c "import sys, json; print(json.load(sys.stdin)['workload_id'])")

# Give child a moment to write log
sleep 0.3
BG_LOGS=$("$MINIBOX" logs "$BG_ID")
if [[ "$BG_LOGS" == *"Hostname: bg-isolated-box"* ]]; then
    pass "Background workload logged isolated hostname 'bg-isolated-box'"
else
    fail "Background UTS isolation" "Logs: $BG_LOGS"
fi

# Test 5: PID namespace isolation
run_test "PID namespace isolation runs process as PID 1"
PID_OUT=$("$MINIBOX" run --pid-ns bin/examples/check_isolation)
if [[ "$PID_OUT" == *"PID:      1"* ]]; then
    pass "Workload confirmed running as PID 1 in dedicated PID namespace"
else
    fail "PID namespace isolation" "Output: $PID_OUT"
fi

# Test 6: Full container isolation (--isolate)
run_test "Full container isolation combines UTS, PID, and Mount namespaces"
FULL_OUT=$("$MINIBOX" run --isolate --hostname full-container-box bin/examples/check_isolation)
if [[ "$FULL_OUT" == *"Hostname: full-container-box"* && "$FULL_OUT" == *"PID:      1"* ]]; then
    pass "Full isolation verified: Hostname 'full-container-box' and PID 1"
else
    fail "Full isolation" "Output: $FULL_OUT"
fi

# Test 7: Workload inspection faithfully reports active isolation features
run_test "Workload inspection reports active namespace features"
INSPECT_JSON=$("$MINIBOX" inspect --json "$BG_ID")
INSPECT_ISO=$(echo "$INSPECT_JSON" | python3 -c "import sys, json; d = json.load(sys.stdin); print(d.get('iso_uts'), d.get('iso_hostname'))")
if [[ "$INSPECT_ISO" == "True bg-isolated-box" ]]; then
    pass "Workload inspection confirmed active UTS isolation and hostname: $INSPECT_ISO"
else
    fail "Inspection isolation" "Got: $INSPECT_ISO"
fi

# Test 8: Non-isolated workload inspection confirms isolation is disabled
run_test "Non-isolated workload inspection confirms isolation disabled"
NO_ISO_JSON=$("$MINIBOX" run --json bin/examples/hello 2>/dev/null)
NO_ISO_CHECK=$(echo "$NO_ISO_JSON" | python3 -c "import sys, json; d = json.load(sys.stdin); print(d.get('iso_uts'), d.get('iso_pid'))")
if [[ "$NO_ISO_CHECK" == "False False" ]]; then
    pass "Non-isolated workload inspection correctly marked iso_uts=False, iso_pid=False"
else
    fail "Non-isolated inspection" "Got: $NO_ISO_CHECK"
fi

# Test 9: Clean termination and reaping of isolated background workload
run_test "Controlled termination of isolated background workload"
ISO_SLEEPER=$("$MINIBOX" start --isolate --hostname isolated-sleeper --json bin/examples/sleeper 10)
ISO_SLEEPER_ID=$(echo "$ISO_SLEEPER" | python3 -c "import sys, json; print(json.load(sys.stdin)['workload_id'])")
ISO_SLEEPER_PID=$(echo "$ISO_SLEEPER" | python3 -c "import sys, json; print(json.load(sys.stdin)['pid'])")

"$MINIBOX" stop --json "$ISO_SLEEPER_ID" >/dev/null
sleep 0.1
if [[ ! -d "/proc/$ISO_SLEEPER_PID" ]]; then
    pass "Isolated workload PID $ISO_SLEEPER_PID terminated and reaped with zero zombies"
else
    fail "Isolated workload stop" "/proc/$ISO_SLEEPER_PID still exists"
fi

echo "=========================================="
if [[ $FAILED -eq 0 ]]; then
    echo " ALL $TOTAL PHASE 3 TESTS PASSED!"
    echo "=========================================="
    exit 0
else
    echo " $FAILED OF $TOTAL TESTS FAILED!"
    echo "=========================================="
    exit 1
fi
