#!/usr/bin/env bash
set -euo pipefail

# Use isolated state directory for testing
export MINIBOX_STATE_DIR="$(mktemp -d /tmp/minibox-test-XXXXXX)/workloads"
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

echo "=========================================="
echo " MiniBox Phase 2 Test Suite"
echo " (Lifecycle & Process Management)"
echo "=========================================="

# Test 1: Start background workload
run_test "Asynchronous background workload launch (minibox start)"
START_JSON=$("$MINIBOX" start --name test-sleeper --json bin/examples/sleeper 10)
PARSED_START=$(echo "$START_JSON" | python3 -c "import sys, json; d = json.load(sys.stdin); print(d.get('ok'), d.get('state'))")
WL_ID=$(echo "$START_JSON" | python3 -c "import sys, json; print(json.load(sys.stdin)['workload_id'])")
WL_PID=$(echo "$START_JSON" | python3 -c "import sys, json; print(json.load(sys.stdin)['pid'])")

if [[ "$PARSED_START" == "True RUNNING" && -n "$WL_ID" && -n "$WL_PID" ]]; then
    pass "Background workload started with state RUNNING, ID: $WL_ID, PID: $WL_PID"
else
    fail "Background start" "Output: $START_JSON"
fi

# Test 2: Check OS process liveness in /proc
run_test "Verify workload process is genuinely running in OS (/proc/<pid>)"
if [[ -d "/proc/$WL_PID" ]]; then
    pass "OS confirms process PID $WL_PID is actively running"
else
    fail "Process liveness" "/proc/$WL_PID does not exist"
fi

# Test 3: List workloads (table and json)
run_test "List active workloads (minibox list)"
LIST_TABLE=$("$MINIBOX" list)
LIST_JSON=$("$MINIBOX" list --json)
FOUND_IN_JSON=$(echo "$LIST_JSON" | python3 -c "import sys, json; list_d = json.load(sys.stdin); print(any(w['id'] == '$WL_ID' and w['state'] == 'RUNNING' for w in list_d))")

if [[ "$LIST_TABLE" == *"$WL_ID"* && "$FOUND_IN_JSON" == "True" ]]; then
    pass "Workload $WL_ID listed correctly in table and JSON formats"
else
    fail "Workload listing" "Table: $LIST_TABLE, JSON: $LIST_JSON"
fi

# Test 4: Inspect workload
run_test "Inspect workload record (minibox inspect)"
INSPECT_JSON=$("$MINIBOX" inspect --json "$WL_ID")
INSPECT_CHECK=$(echo "$INSPECT_JSON" | python3 -c "import sys, json; d = json.load(sys.stdin); print(d.get('ok'), d.get('name'), d.get('state'), d.get('pid'))")

if [[ "$INSPECT_CHECK" == "True test-sleeper RUNNING $WL_PID" ]]; then
    pass "Inspection returned exact workload metadata"
else
    fail "Inspection check" "Got: $INSPECT_CHECK"
fi

# Test 5: Verify workload output logs
run_test "Workload stdout log capture (minibox logs)"
# Give sleeper process a moment to write initial line
sleep 0.2
LOGS_OUT=$("$MINIBOX" logs "$WL_ID")
if [[ "$LOGS_OUT" == *"[sleeper] Started PID"* ]]; then
    pass "Workload output was captured in log file: $LOGS_OUT"
else
    fail "Log capture" "Log output: $LOGS_OUT"
fi

# Test 6: Controlled graceful termination (minibox stop)
run_test "Controlled termination with SIGTERM (minibox stop)"
STOP_OUT=$("$MINIBOX" stop --json "$WL_ID")
STOP_OK=$(echo "$STOP_OUT" | python3 -c "import sys, json; d = json.load(sys.stdin); print(d.get('ok'))")

if [[ "$STOP_OK" == "True" ]]; then
    pass "minibox stop command reported success"
else
    fail "Stop command" "Output: $STOP_OUT"
fi

# Verify process is no longer running in /proc
sleep 0.1
if [[ ! -d "/proc/$WL_PID" ]]; then
    pass "Process PID $WL_PID successfully terminated and reaped from kernel"
else
    fail "Process reaping after stop" "/proc/$WL_PID still exists"
fi

# Test 7: Verify state transition to TERMINATED/COMPLETED
run_test "Verify post-stop lifecycle state transition"
POST_STOP_JSON=$("$MINIBOX" inspect --json "$WL_ID")
POST_STATE=$(echo "$POST_STOP_JSON" | python3 -c "import sys, json; print(json.load(sys.stdin)['state'])")
if [[ "$POST_STATE" == "TERMINATED" || "$POST_STATE" == "COMPLETED" ]]; then
    pass "Lifecycle state transitioned to terminal state: $POST_STATE"
else
    fail "Post-stop state" "Expected TERMINATED or COMPLETED, got $POST_STATE"
fi

# Test 8: Force stop (minibox stop --force)
run_test "Forced termination (minibox stop --force)"
FORCE_START=$("$MINIBOX" start --name force-test --json bin/examples/sleeper 60)
FORCE_ID=$(echo "$FORCE_START" | python3 -c "import sys, json; print(json.load(sys.stdin)['workload_id'])")
FORCE_PID=$(echo "$FORCE_START" | python3 -c "import sys, json; print(json.load(sys.stdin)['pid'])")

"$MINIBOX" stop --force "$FORCE_ID" >/dev/null
sleep 0.1

if [[ ! -d "/proc/$FORCE_PID" ]]; then
    pass "Forced termination killed process PID $FORCE_PID immediately"
else
    fail "Force termination" "/proc/$FORCE_PID still exists"
fi

# Test 9: Natural completion of background workload
run_test "Natural completion of background workload"
NATURAL_START=$("$MINIBOX" start --name natural-exit --json bin/examples/sleeper 1)
NAT_ID=$(echo "$NATURAL_START" | python3 -c "import sys, json; print(json.load(sys.stdin)['workload_id'])")
NAT_PID=$(echo "$NATURAL_START" | python3 -c "import sys, json; print(json.load(sys.stdin)['pid'])")

# Wait for 1.3 seconds for sleeper to finish on its own
sleep 1.3
NAT_INSPECT=$("$MINIBOX" inspect --json "$NAT_ID")
NAT_STATE=$(echo "$NAT_INSPECT" | python3 -c "import sys, json; d = json.load(sys.stdin); print(d.get('state'), d.get('exit_code'))")

if [[ "$NAT_STATE" == "COMPLETED 0" && ! -d "/proc/$NAT_PID" ]]; then
    pass "Background workload naturally completed with exit code 0 and state COMPLETED"
else
    fail "Natural completion" "Got: $NAT_STATE, /proc/$NAT_PID exists: $(test -d "/proc/$NAT_PID" && echo yes || echo no)"
fi

# Test 10: Failed background startup handling
run_test "Handle invalid executable in background start"
set +e
FAIL_START=$("$MINIBOX" start --json /nonexistent/binary_xyz_123 2>/dev/null)
FAIL_CODE=$?
set -e

PARSED_FAIL_START=$(echo "$FAIL_START" | python3 -c "import sys, json; d = json.load(sys.stdin); print(d.get('ok'), d.get('state'), d.get('errno'))")
if [[ $FAIL_CODE -eq 127 && "$PARSED_FAIL_START" == "False FAILED 2" ]]; then
    pass "Reported launch failure: returned 127, state FAILED, errno 2 (ENOENT)"
else
    fail "Invalid binary start" "Code: $FAIL_CODE, Parsed: $PARSED_FAIL_START"
fi

# Test 11: Safety - stop non-existent workload ID
run_test "Safety: stopping nonexistent workload ID"
set +e
STOP_ERR=$("$MINIBOX" stop nonexistent-id-999 2>&1)
STOP_ERR_CODE=$?
set -e
if [[ $STOP_ERR_CODE -ne 0 ]]; then
    pass "Attempting to stop unknown workload returned clean error without signalling other processes"
else
    fail "Stop unknown workload" "Unexpected success"
fi

# Test 12: Workload record deletion & cleanup
run_test "Workload deletion and bulk cleanup"
DEL_OUT=$("$MINIBOX" delete --json "$NAT_ID")
DEL_OK=$(echo "$DEL_OUT" | python3 -c "import sys, json; print(json.load(sys.stdin)['ok'])")
if [[ "$DEL_OK" == "True" ]]; then
    pass "Successfully deleted workload record $NAT_ID"
else
    fail "Workload delete" "Output: $DEL_OUT"
fi

CLEANUP_OUT=$("$MINIBOX" cleanup --json)
CLEANUP_OK=$(echo "$CLEANUP_OUT" | python3 -c "import sys, json; print(json.load(sys.stdin)['ok'])")
REMAINING_COUNT=$("$MINIBOX" list --json | python3 -c "import sys, json; print(len(json.load(sys.stdin)))")

if [[ "$CLEANUP_OK" == "True" && "$REMAINING_COUNT" -eq 0 ]]; then
    pass "Bulk cleanup removed all finished workloads; 0 workloads remaining"
else
    fail "Bulk cleanup" "Cleanup: $CLEANUP_OUT, remaining: $REMAINING_COUNT"
fi

echo "=========================================="
if [[ $FAILED -eq 0 ]]; then
    echo " ALL $TOTAL PHASE 2 TESTS PASSED!"
    echo "=========================================="
    exit 0
else
    echo " $FAILED OF $TOTAL TESTS FAILED!"
    echo "=========================================="
    exit 1
fi
