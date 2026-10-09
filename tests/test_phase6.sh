#!/usr/bin/env bash
# MiniBox Phase 6 Automated Test Suite
# Tests Interactive Terminal UI, Textual application, and launcher

set -uo pipefail

TARGET="./minibox-tui"
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
echo "Running MiniBox Phase 6 Test Suite"
echo " (Interactive Terminal UI with Textual)"
echo "=================================================="

# Test 1: Launcher executable presence and permissions
info "Test 1: Launcher executable presence and permissions"
if [ -x "$TARGET" ]; then
    pass "Launcher script $TARGET exists and is executable"
else
    fail "Launcher" "$TARGET is missing or not executable"
fi

# Test 2: Launcher reports version
info "Test 2: Launcher reports version"
VER_OUT=$("$TARGET" --version)
if echo "$VER_OUT" | grep -E -q "0\.[6-9]\.[0-9]-phase"; then
    pass "Launcher version output contains valid phase version ($VER_OUT)"
else
    fail "Launcher version" "Got: $VER_OUT"
fi

# Test 3: Launcher displays help information
info "Test 3: Launcher displays help information"
HELP_OUT=$("$TARGET" --help)
if echo "$HELP_OUT" | grep -q "MiniBox TUI" && echo "$HELP_OUT" | grep -q "Usage:"; then
    pass "Launcher displays interactive dashboard usage instructions"
else
    fail "Launcher help" "Got: $HELP_OUT"
fi

# Test 4: Python Textual test suite via pytest
info "Test 4: Running Textual application headless test harness (pytest)"
PYTEST_BIN=".venv/bin/pytest"
if [ ! -x "$PYTEST_BIN" ]; then
    PYTEST_BIN="$(command -v pytest || true)"
fi

if [ -n "$PYTEST_BIN" ] && [ -x "$PYTEST_BIN" ]; then
    PYTEST_OUT=$(PYTHONPATH=. "$PYTEST_BIN" tests/test_tui.py 2>&1)
    PYTEST_RC=$?
    if [ "$PYTEST_RC" -eq 0 ]; then
        pass "All Textual App headless pilot tests passed successfully"
    else
        fail "Textual pytest suite" "Failed with code $PYTEST_RC: $PYTEST_OUT"
    fi
else
    fail "pytest" "pytest binary not found in .venv or PATH"
fi

# Test 5: Verify C runtime - TUI client communication end-to-end
info "Test 5: Verify C runtime and client communication end-to-end"
CLIENT_TEST=$(PYTHONPATH=. .venv/bin/python3 -c "
import asyncio
from tui.client import MiniBoxClient

async def test():
    client = MiniBoxClient()
    caps = await client.get_capabilities()
    if not caps.user_namespaces:
        return 'FAIL: user_namespaces not detected'
    ok, msg, data = await client.start_workload('./bin/examples/sleeper', ['5'], name='tui-smoke-test')
    if not ok:
        return f'FAIL: {msg}'
    wl_id = data['workload_id']
    detail = await client.get_workload_detail(wl_id)
    if not detail or detail.state != 'RUNNING':
        return f'FAIL: expected RUNNING, got {detail.state if detail else None}'
    stop_ok, stop_msg = await client.stop_workload(wl_id)
    if not stop_ok:
        return f'FAIL: {stop_msg}'
    clean_ok, clean_msg = await client.cleanup_workloads()
    return 'OK'

print(asyncio.run(test()))
" 2>&1)

if [ "$CLIENT_TEST" = "OK" ]; then
    pass "End-to-end client communication, start, inspect, stop, and cleanup verified"
else
    fail "End-to-end client test" "Got: $CLIENT_TEST"
fi

# Test 6: Workload cleanup check
info "Test 6: Verify zero leftover workloads"
LEFTOVER=$("$CORE_TARGET" list --json)
WL_COUNT=$(python3 -c "import sys, json; data = json.load(sys.stdin); print(len(data) if isinstance(data, list) else len(data.get('workloads', [])))" <<< "$LEFTOVER")
if [ "$WL_COUNT" -eq 0 ]; then
    pass "Zero leftover workloads after test execution"
else
    fail "Leftover workloads" "$WL_COUNT workloads still present"
fi

echo "=================================================="
echo "Phase 6 Test Results: $PASSED passed, $FAILED failed"
echo "=================================================="

if [ "$FAILED" -gt 0 ]; then
    exit 1
fi
exit 0
