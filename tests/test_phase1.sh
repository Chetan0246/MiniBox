#!/usr/bin/env bash
set -euo pipefail

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
echo " MiniBox Phase 1 Test Suite"
echo "=========================================="

# Test 1: CLI version and help flags
run_test "CLI version information"
VERSION_OUT=$("$MINIBOX" version)
if [[ "$VERSION_OUT" == *"minibox version"* ]]; then
    pass "Reported version correctly: $VERSION_OUT"
else
    fail "Version check" "Unexpected output: $VERSION_OUT"
fi

run_test "CLI help information"
HELP_OUT=$("$MINIBOX" --help 2>&1 || true)
if [[ "$HELP_OUT" == *"Usage:"* ]]; then
    pass "Displayed usage help"
else
    fail "Help check" "Unexpected output: $HELP_OUT"
fi

# Test 2: Valid workload execution (exit code 0)
run_test "Launch valid example executable (hello)"
OUTPUT=$("$MINIBOX" run bin/examples/hello)
EXIT_CODE=$?
if [[ $EXIT_CODE -eq 0 && "$OUTPUT" == *"Hello from MiniBox example workload!"* ]]; then
    pass "Child executed and completed with exit code 0"
else
    fail "Valid workload" "Exit code $EXIT_CODE, output: $OUTPUT"
fi

# Test 3: Argument forwarding
run_test "Argument passing to child process"
OUTPUT=$("$MINIBOX" run bin/examples/hello alpha beta)
if [[ "$OUTPUT" == *"arg[1]: alpha"* && "$OUTPUT" == *"arg[2]: beta"* ]]; then
    pass "Arguments passed accurately to child"
else
    fail "Argument passing" "Output: $OUTPUT"
fi

# Test 4: Exit code collection for nonzero exit
run_test "Collect child non-zero exit code (exit_code 42)"
set +e
"$MINIBOX" run bin/examples/exit_code 42 >/dev/null 2>&1
EXIT_CODE=$?
set -e
if [[ $EXIT_CODE -eq 42 ]]; then
    pass "Non-zero exit code 42 collected correctly"
else
    fail "Non-zero exit code" "Expected 42, got $EXIT_CODE"
fi

# Test 5: System command execution (/usr/bin/true and /usr/bin/false)
run_test "System commands (/usr/bin/true and /usr/bin/false)"
"$MINIBOX" run /usr/bin/true >/dev/null
set +e
"$MINIBOX" run /usr/bin/false >/dev/null 2>&1
FALSE_CODE=$?
set -e
if [[ $FALSE_CODE -eq 1 ]]; then
    pass "/usr/bin/false returned status code 1"
else
    fail "False return code" "Expected 1, got $FALSE_CODE"
fi

# Test 6: Nonexistent executable handling (error pipe detection)
run_test "Invalid executable error handling (ENOENT)"
set +e
ERR_OUT=$("$MINIBOX" run /nonexistent_binary_xyz_123 2>&1)
ERR_CODE=$?
set -e
if [[ $ERR_CODE -eq 127 && "$ERR_OUT" == *"No such file or directory"* ]]; then
    pass "Handled nonexistent binary cleanly without crash; returned 127 with errno message"
else
    fail "Nonexistent binary" "Exit code $ERR_CODE, output: $ERR_OUT"
fi

# Test 7: Non-executable file (EACCES)
run_test "Permission denied executable handling (EACCES)"
TEMP_NOEXEC=$(mktemp ./build/noexec.XXXXXX)
chmod -x "$TEMP_NOEXEC"
set +e
ERR_OUT=$("$MINIBOX" run "$TEMP_NOEXEC" 2>&1)
ERR_CODE=$?
rm -f "$TEMP_NOEXEC"
set -e
if [[ $ERR_CODE -eq 127 && "$ERR_OUT" == *"Permission denied"* ]]; then
    pass "Handled non-executable file with EACCES cleanly; returned 127"
else
    fail "Permission denied" "Exit code $ERR_CODE, output: $ERR_OUT"
fi

# Test 8: Child process signal termination
run_test "Child terminated by unhandled signal (SIGSEGV)"
set +e
SIGNAL_OUT=$("$MINIBOX" run bin/examples/crash_test 11 2>&1)
SIGNAL_CODE=$?
set -e
# Fatal signal 11 corresponds to exit status 128 + 11 = 139
if [[ $SIGNAL_CODE -eq 139 && "$SIGNAL_OUT" == *"Terminated by signal: 11"* ]]; then
    pass "Detected child signal termination (SIGSEGV 11, status code 139)"
else
    fail "Signal termination" "Exit code $SIGNAL_CODE, output: $SIGNAL_OUT"
fi

# Test 9: Child process reaping & zombie prevention
run_test "Zombie check: verify child process is fully reaped"
JSON_OUT=$("$MINIBOX" run --json bin/examples/hello 2>/dev/null)
CHILD_PID=$(echo "$JSON_OUT" | python3 -c "import sys, json; print(json.load(sys.stdin)['pid'])")
if [[ -n "$CHILD_PID" && ! -d "/proc/$CHILD_PID" ]]; then
    pass "Child PID $CHILD_PID does not exist in /proc after exit (no zombie)"
else
    fail "Zombie check" "PID $CHILD_PID still exists in /proc or invalid PID"
fi

# Test 10: JSON protocol output for success and failure
run_test "Structured JSON output validation"
set +e
SUCCESS_JSON=$("$MINIBOX" run --json bin/examples/exit_code 7 2>/dev/null)
JSON_CODE=$?
set -e
PARSED_CODE=$(echo "$SUCCESS_JSON" | python3 -c "import sys, json; d = json.load(sys.stdin); print(d.get('ok'), d.get('exit_code'), d.get('status'))")
if [[ $JSON_CODE -eq 7 && "$PARSED_CODE" == "True 7 COMPLETED" ]]; then
    pass "Successful run yielded valid JSON structure with correct exit_code 7"
else
    fail "JSON success validation" "Got code $JSON_CODE, parsed: $PARSED_CODE"
fi

set +e
FAIL_JSON=$("$MINIBOX" run --json /nonexistent_binary_xyz_123 2>/dev/null)
set -e
PARSED_FAIL=$(echo "$FAIL_JSON" | python3 -c "import sys, json; d = json.load(sys.stdin); print(d.get('ok'), d.get('status'), d.get('errno'))")
if [[ "$PARSED_FAIL" == "False FAILED 2" ]]; then
    pass "Failed run yielded valid JSON structure with status FAILED and errno 2 (ENOENT)"
else
    fail "JSON failure validation" "Got: $PARSED_FAIL"
fi

# Test 11: Working directory option
run_test "Working directory configuration (--workdir)"
PWD_OUT=$("$MINIBOX" run --workdir /tmp /usr/bin/pwd)
if [[ "$PWD_OUT" == *"/tmp"* ]]; then
    pass "Child executed in configured working directory (/tmp)"
else
    fail "Working directory" "Output: $PWD_OUT"
fi

echo "=========================================="
if [[ $FAILED -eq 0 ]]; then
    echo " ALL $TOTAL TESTS PASSED SUCCESSFULLY!"
    echo "=========================================="
    exit 0
else
    echo " $FAILED OF $TOTAL TESTS FAILED!"
    echo "=========================================="
    exit 1
fi
