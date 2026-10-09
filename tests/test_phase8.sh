#!/usr/bin/env bash
# ==============================================================================
# MiniBox Phase 8 Automated Test Suite
# Tests Final Integration, Documentation Deliverables, and Demonstration
# ==============================================================================

set -uo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CORE_TARGET="$REPO_ROOT/bin/minibox"
DEMO_SCRIPT="$REPO_ROOT/demo/demo_script.sh"
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
echo "Running MiniBox Phase 8 Test Suite"
echo " (Final Integration & Documentation Verification)"
echo "=================================================="

# Test 1: Core Documentation Files
info "Test 1: Core Documentation Deliverables Presence and Non-Emptiness"
DOCS=(
    "README.md"
    "docs/architecture.md"
    "docs/design.md"
    "docs/user_manual.md"
    "docs/testing.md"
    "docs/performance_report.md"
    "docs/viva_questions.md"
    "docs/final_report.md"
)

DOCS_OK=true
for doc in "${DOCS[@]}"; do
    if [ -f "$REPO_ROOT/$doc" ] && [ -s "$REPO_ROOT/$doc" ]; then
        :
    else
        DOCS_OK=false
        fail "Documentation File" "$doc is missing or empty"
    fi
done

if [ "$DOCS_OK" = true ]; then
    pass "All 8 core documentation files exist and are non-empty"
fi

# Test 2: Content Verification in Documentation
info "Test 2: Key Content & Section Verification in Deliverables"
CONTENT_OK=true

# Check README.md
if ! grep -q "MiniBox TUI: An Educational Linux Container Runtime" "$REPO_ROOT/README.md" || \
   ! grep -q "Dashboard Keybindings" "$REPO_ROOT/README.md"; then
    CONTENT_OK=false
    fail "README.md" "Missing title or keybindings section"
fi

# Check architecture.md
if ! grep -q "High-Level Architecture Diagram" "$REPO_ROOT/docs/architecture.md" || \
   ! grep -q "Linux Kernel System Call Footprint" "$REPO_ROOT/docs/architecture.md"; then
    CONTENT_OK=false
    fail "architecture.md" "Missing architecture diagram or system call footprint"
fi

# Check design.md
if ! grep -q "Workload Lifecycle State Machine" "$REPO_ROOT/docs/design.md" || \
   ! grep -q "Process Launch Sequence" "$REPO_ROOT/docs/design.md"; then
    CONTENT_OK=false
    fail "design.md" "Missing lifecycle state machine or launch sequence"
fi

# Check user_manual.md
if ! grep -q "Terminal Interface Navigation" "$REPO_ROOT/docs/user_manual.md" || \
   ! grep -q "Troubleshooting & FAQ" "$REPO_ROOT/docs/user_manual.md"; then
    CONTENT_OK=false
    fail "user_manual.md" "Missing navigation or FAQ section"
fi

# Check testing.md
if ! grep -q "Test Plan Matrix (T01 – T15)" "$REPO_ROOT/docs/testing.md"; then
    CONTENT_OK=false
    fail "testing.md" "Missing T01-T15 test plan matrix"
fi

# Check viva_questions.md
if ! grep -q "What happens under the hood during \`fork()\`?" "$REPO_ROOT/docs/viva_questions.md" || \
   ! grep -q "How does the Linux Completely Fair Scheduler (CFS) enforce CPU quotas?" "$REPO_ROOT/docs/viva_questions.md"; then
    CONTENT_OK=false
    fail "viva_questions.md" "Missing required viva questions"
fi

# Check final_report.md
if ! grep -q "Course Project Final Technical Report" "$REPO_ROOT/docs/final_report.md" || \
   ! grep -q "Abstract" "$REPO_ROOT/docs/final_report.md"; then
    CONTENT_OK=false
    fail "final_report.md" "Missing technical report title or abstract"
fi

if [ "$CONTENT_OK" = true ]; then
    pass "Documentation content verified with required architectural sections and matrices"
fi

# Test 3: Demonstration Script Presence and Executable Flag
info "Test 3: Demonstration Script Presence & Permissions"
if [ -x "$DEMO_SCRIPT" ]; then
    pass "Demonstration script $DEMO_SCRIPT exists and is executable"
else
    fail "Demonstration Script" "$DEMO_SCRIPT is missing or not executable"
fi

# Test 4: Run Demonstration Script End-to-End
info "Test 4: Execute Complete 13-Step Demonstration Scenario"
DEMO_OUT=$(bash "$DEMO_SCRIPT" 2>&1)
DEMO_RC=$?
if [ "$DEMO_RC" -eq 0 ] && echo "$DEMO_OUT" | grep -q "MiniBox Demonstration Successfully Completed!"; then
    pass "13-step demonstration scenario executed successfully (exit code 0)"
else
    fail "Demonstration Execution" "Failed with code $DEMO_RC: $DEMO_OUT"
fi

# Test 5: Verify Zero Lingering Workloads or Orphaned Resources
info "Test 5: Verify Zero Lingering Workloads after Demonstration"
PYTHON_EXEC="$REPO_ROOT/.venv/bin/python3"
if [ ! -x "$PYTHON_EXEC" ]; then
    PYTHON_EXEC="$(command -v python3)"
fi

LEFTOVER=$("$CORE_TARGET" list --json)
WL_COUNT=$("$PYTHON_EXEC" -c "import sys, json; data = json.load(sys.stdin); print(len(data) if isinstance(data, list) else len(data.get('workloads', [])))" <<< "$LEFTOVER")
if [ "$WL_COUNT" -eq 0 ]; then
    pass "Zero leftover workloads after demonstration execution"
else
    fail "Leftover workloads" "$WL_COUNT workloads still present"
fi

echo "=================================================="
echo "Phase 8 Test Results: $PASSED passed, $FAILED failed"
echo "=================================================="

if [ "$FAILED" -gt 0 ]; then
    exit 1
fi
exit 0
