#!/usr/bin/env bash
# ==============================================================================
# MiniBox Regression Test Suite
#
# Each test here corresponds to a defect found during the correctness audit and
# subsequently fixed. Every test fails against the pre-fix runtime.
# ==============================================================================

set -uo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TARGET="$REPO_ROOT/bin/minibox"
HELLO="$REPO_ROOT/bin/examples/hello"
SLEEPER="$REPO_ROOT/bin/examples/sleeper"

FAILED=0
PASSED=0

RED='\033[0;31m'
GREEN='\033[0;32m'
BLUE='\033[0;34m'
NC='\033[0m'

pass() { echo -e "${GREEN}[PASS]${NC} $1"; PASSED=$((PASSED + 1)); }
fail() { echo -e "${RED}[FAIL]${NC} $1: $2"; FAILED=$((FAILED + 1)); }
info() { echo -e "${BLUE}[INFO]${NC} $1"; }

# Each test group gets an isolated state directory so leftovers cannot leak between them.
STATE_DIR="$(mktemp -d /tmp/minibox-regress-XXXXXX)"
export MINIBOX_STATE_DIR="$STATE_DIR"

cleanup() {
    "$TARGET" cleanup >/dev/null 2>&1
    "$TARGET" delete --force "$(ls "$STATE_DIR" 2>/dev/null | head -1)" >/dev/null 2>&1
    rm -rf "$STATE_DIR"
}
trap cleanup EXIT

json_field() { python3 -c "import json,sys;print(json.load(sys.stdin).get('$1'))" 2>/dev/null; }

# ==============================================================================
info "Test 1: state records larger than 4 KiB remain loadable"
# The loader used a fixed 4096-byte buffer while a valid record can exceed 7 KiB,
# which made oversized workloads invisible to every command and leaked their
# directory, log and cgroup permanently.
# ==============================================================================
LONG_STATE_DIR="/tmp"
for _ in $(seq 1 8); do LONG_STATE_DIR="$LONG_STATE_DIR/$(printf 'q%.0s' $(seq 1 60))"; done
mkdir -p "$LONG_STATE_DIR"
{
    local_path="/tmp"
    for _ in $(seq 1 14); do local_path="$local_path/$(printf 'd%.0s' $(seq 1 66))"; done
    mkdir -p "$local_path"
    A1="$(python3 -c "print('a'*1000)")"
    A2="$(python3 -c "print('b'*1000)")"
    A3="$(python3 -c "print('c'*47)")"
    MINIBOX_STATE_DIR="$LONG_STATE_DIR" "$TARGET" run --id bigrec --workdir "$local_path/nope" \
        "$HELLO" "$A1" "$A2" "$A3" >/dev/null 2>&1
    REC_SIZE=$(stat -c%s "$LONG_STATE_DIR/bigrec/state.json" 2>/dev/null || echo 0)
    if [ "$REC_SIZE" -gt 4095 ]; then
        INSPECT_OUT=$(MINIBOX_STATE_DIR="$LONG_STATE_DIR" "$TARGET" inspect --json bigrec 2>&1)
        if echo "$INSPECT_OUT" | python3 -c "import json,sys;sys.exit(0 if json.load(sys.stdin).get('ok') else 1)" 2>/dev/null; then
            pass "Oversized state.json (${REC_SIZE} bytes > 4095) remains loadable"
        else
            fail "Oversized state.json loadable" "inspect failed for a ${REC_SIZE}-byte record"
        fi
        LISTED=$(MINIBOX_STATE_DIR="$LONG_STATE_DIR" "$TARGET" list --json 2>/dev/null | grep -c '"id"')
        if [ "$LISTED" -ge 1 ]; then
            pass "Oversized state.json is listed by 'list'"
        else
            fail "Oversized state.json listed" "workload missing from list output"
        fi
    else
        fail "Oversized state.json" "could not produce a record over 4095 bytes (got ${REC_SIZE})"
    fi
    rm -rf "$LONG_STATE_DIR"
}

# ==============================================================================
info "Test 2: an explicit --id that already exists is rejected"
# Reusing an id silently overwrote the previous workload's metadata and adopted
# its cgroup and log file.
# ==============================================================================
"$TARGET" run --id dupcheck "$HELLO" >/dev/null 2>&1
FIRST_NAME=$("$TARGET" inspect --json dupcheck 2>/dev/null | python3 -c "import json,sys;print(json.load(sys.stdin)['name'])" 2>/dev/null)
SECOND_OUT=$("$TARGET" run --id dupcheck --name SECOND "$HELLO" 2>&1)
SECOND_RC=$?
AFTER_NAME=$("$TARGET" inspect --json dupcheck 2>/dev/null | python3 -c "import json,sys;print(json.load(sys.stdin)['name'])" 2>/dev/null)
if [ "$SECOND_RC" -ne 0 ] && echo "$SECOND_OUT" | grep -qi "already exists" && [ "$AFTER_NAME" = "$FIRST_NAME" ]; then
    pass "Duplicate --id rejected and original record preserved"
else
    fail "Duplicate --id rejected" "rc=$SECOND_RC name '$FIRST_NAME' -> '$AFTER_NAME'"
fi
"$TARGET" delete --force dupcheck >/dev/null 2>&1

# ==============================================================================
info "Test 3: stop reclaims the cgroup when the supervisor has been killed"
# With no supervisor alive nothing performed cgroup cleanup, so every orphaned
# workload leaked a cgroup directory that only 'delete' could reclaim.
# ==============================================================================
CG_DIR=$("$TARGET" start --json --id orphan --memory 64M "$SLEEPER" 300 2>/dev/null | python3 -c "import json,sys;print(json.load(sys.stdin)['cgroup_path'])" 2>/dev/null)
if [ -n "$CG_DIR" ] && [ -d "$CG_DIR" ]; then
    SUP=$("$TARGET" inspect --json orphan 2>/dev/null | python3 -c "import json,sys;print(json.load(sys.stdin)['supervisor_pid'])" 2>/dev/null)
    CHILD=$("$TARGET" inspect --json orphan 2>/dev/null | python3 -c "import json,sys;print(json.load(sys.stdin)['pid'])" 2>/dev/null)
    kill -9 "$SUP" 2>/dev/null
    sleep 0.5
    "$TARGET" stop orphan >/dev/null 2>&1
    sleep 0.5
    if [ ! -d "$CG_DIR" ]; then
        pass "Orphaned workload cgroup reclaimed by 'stop'"
    else
        fail "Orphaned workload cgroup reclaimed" "cgroup still present at $CG_DIR"
    fi
    if [ -z "$CHILD" ] || [ ! -d "/proc/$CHILD" ]; then
        pass "Orphaned workload process terminated by 'stop'"
    else
        kill -9 "$CHILD" 2>/dev/null
        fail "Orphaned workload process terminated" "pid $CHILD still alive"
    fi
    TERM_SIG=$("$TARGET" inspect --json orphan 2>/dev/null | python3 -c "import json,sys;print(json.load(sys.stdin)['term_signal'])" 2>/dev/null)
    if [ "$TERM_SIG" = "15" ]; then
        pass "Stop records the signal actually delivered (term_signal=15)"
    else
        fail "Stop records delivered signal" "term_signal='$TERM_SIG', expected 15"
    fi
    "$TARGET" delete --force orphan >/dev/null 2>&1
else
    fail "Orphaned workload cgroup reclaimed" "cgroup delegation unavailable on this host"
fi

# ==============================================================================
info "Test 4: a supervised stop keeps the supervisor's authoritative result"
# The synthesised TERMINATED record must not overwrite a real exit code.
# ==============================================================================
"$TARGET" start --json --id supervised "$SLEEPER" 60 >/dev/null 2>&1
sleep 0.4
"$TARGET" stop supervised >/dev/null 2>&1
sleep 0.3
SUP_STATE=$("$TARGET" inspect --json supervised 2>/dev/null | python3 -c "import json,sys;d=json.load(sys.stdin);print(d['state'],d['exit_code'])" 2>/dev/null)
if [ "$SUP_STATE" = "COMPLETED 0" ]; then
    pass "Supervised stop preserves real exit code (COMPLETED, exit 0)"
else
    fail "Supervised stop preserves exit code" "got '$SUP_STATE'"
fi
"$TARGET" delete --force supervised >/dev/null 2>&1

# ==============================================================================
info "Test 5: --sample-ms cannot overflow or block indefinitely"
# 'sample_window_ms * 1000' overflowed int, and no upper bound was enforced.
# ==============================================================================
"$TARGET" start --json --id sampler "$SLEEPER" 30 >/dev/null 2>&1
sleep 0.3
OVERFLOW_OUT=$(timeout 5 "$TARGET" metrics --sample-ms 3000000 sampler 2>&1)
OVERFLOW_RC=$?
if [ "$OVERFLOW_RC" -ne 124 ] && echo "$OVERFLOW_OUT" | grep -qi "invalid sample-ms"; then
    pass "Oversized --sample-ms rejected instead of overflowing"
else
    fail "Oversized --sample-ms rejected" "rc=$OVERFLOW_RC out='$OVERFLOW_OUT'"
fi
BIG_OUT=$(timeout 5 "$TARGET" metrics --sample-ms 100000 sampler 2>&1)
if [ $? -ne 124 ] && echo "$BIG_OUT" | grep -qi "invalid sample-ms"; then
    pass "Unbounded --sample-ms rejected instead of blocking"
else
    fail "Unbounded --sample-ms rejected" "out='$BIG_OUT'"
fi
OK_OUT=$(timeout 10 "$TARGET" metrics --json --sample-ms 100 sampler 2>/dev/null)
if echo "$OK_OUT" | python3 -c "import json,sys;sys.exit(0 if json.load(sys.stdin).get('ok') else 1)" 2>/dev/null; then
    pass "Valid --sample-ms still accepted"
else
    fail "Valid --sample-ms accepted" "metrics call failed"
fi
"$TARGET" delete --force sampler >/dev/null 2>&1

# ==============================================================================
info "Test 6: metrics cost does not scale with workload count"
# Host CPU was re-measured per workload, costing ~50 ms each and overrunning the
# TUI's 1.5 s refresh interval past roughly 30 workloads.
# ==============================================================================
for i in 1 2 3 4 5 6; do "$TARGET" start --json --id scale$i "$SLEEPER" 60 >/dev/null 2>&1; done
sleep 0.4
START_NS=$(date +%s%N)
"$TARGET" metrics --json >/dev/null 2>&1
END_NS=$(date +%s%N)
ELAPSED_MS=$(( (END_NS - START_NS) / 1000000 ))
if [ "$ELAPSED_MS" -lt 400 ]; then
    pass "metrics --json stays fast with 6 workloads (${ELAPSED_MS} ms)"
else
    fail "metrics --json latency" "${ELAPSED_MS} ms for 6 workloads (was ~50 ms per workload)"
fi
for i in 1 2 3 4 5 6; do "$TARGET" delete --force scale$i >/dev/null 2>&1; done

# ==============================================================================
info "Test 7: state writes leave no shared temporary file behind"
# A fixed state.json.tmp let concurrent writers interleave into one inode.
# ==============================================================================
for i in 1 2 3 4; do "$TARGET" start --json --id tmpchk$i "$SLEEPER" 30 >/dev/null 2>&1; done
for _ in $(seq 1 60); do "$TARGET" list --json >/dev/null 2>&1; done
LEFTOVER=$(find "$STATE_DIR" -name 'state.json.tmp' 2>/dev/null | wc -l)
if [ "$LEFTOVER" -eq 0 ]; then
    pass "No shared state.json.tmp artefacts left after concurrent writes"
else
    fail "No shared temp artefacts" "$LEFTOVER leftover state.json.tmp file(s)"
fi
for i in 1 2 3 4; do "$TARGET" delete --force tmpchk$i >/dev/null 2>&1; done

# ==============================================================================
info "Test 8: commands reject more than one workload id"
# The last non-flag argument silently won, so 'inspect a b' inspected b.
# ==============================================================================
"$TARGET" run --id arga "$HELLO" >/dev/null 2>&1
"$TARGET" run --id argb "$HELLO" >/dev/null 2>&1
OUT=$("$TARGET" inspect arga argb 2>&1)
if echo "$OUT" | grep -qi "unexpected extra argument"; then
    pass "Extra positional argument rejected by inspect"
else
    fail "Extra positional rejected (inspect)" "got: $OUT"
fi
OUT=$("$TARGET" metrics arga argb 2>&1)
if echo "$OUT" | grep -qi "unexpected extra argument"; then
    pass "Extra positional argument rejected by metrics"
else
    fail "Extra positional rejected (metrics)" "got: $OUT"
fi
OUT=$("$TARGET" stop arga argb 2>&1)
if echo "$OUT" | grep -qi "unexpected extra argument"; then
    pass "Extra positional argument rejected by stop"
else
    fail "Extra positional rejected (stop)" "got: $OUT"
fi
"$TARGET" delete --force arga >/dev/null 2>&1
"$TARGET" delete --force argb >/dev/null 2>&1

# ==============================================================================
info "Test 9: usage only advertises options each command accepts"
# --sample-ms, --force and --signal were documented globally but rejected by
# run/start as unknown options.
# ==============================================================================
USAGE=$("$TARGET" help 2>&1)
# The run/start option block runs from its heading up to the next heading.
RUN_OPTS=$(echo "$USAGE" | awk "/^Options for 'run' and 'start':/{f=1;next} /^Options for/{f=0} f")
LEAKED=""
for opt in --sample-ms --force --signal; do
    if echo "$RUN_OPTS" | grep -q -- "$opt"; then LEAKED="$LEAKED $opt"; fi
done
if [ -z "$LEAKED" ]; then
    pass "run/start usage omits --sample-ms, --force and --signal"
else
    fail "run/start usage accurate" "still advertises:$LEAKED"
fi
UNSHOWN=""
for opt in --sample-ms --force --signal; do
    if ! echo "$USAGE" | grep -q -- "$opt"; then UNSHOWN="$UNSHOWN $opt"; fi
done
if [ -z "$UNSHOWN" ]; then
    pass "Every documented option is still discoverable in help"
else
    fail "Documented options discoverable" "missing from help:$UNSHOWN"
fi

# ==============================================================================
info "Test 10: delete removes workload directories containing subdirectories"
# remove_directory_recursive only unlinked files, so any nested directory made
# the whole record undeletable.
# ==============================================================================
"$TARGET" run --id nested "$HELLO" >/dev/null 2>&1
mkdir -p "$STATE_DIR/nested/deeper" && touch "$STATE_DIR/nested/deeper/artifact"
DEL_OUT=$("$TARGET" delete --force nested 2>&1)
DEL_RC=$?
if [ "$DEL_RC" -eq 0 ] && [ ! -d "$STATE_DIR/nested" ]; then
    pass "delete removes records containing nested directories"
else
    fail "Recursive delete" "rc=$DEL_RC out='$DEL_OUT'"
fi

# ==============================================================================
info "Test 11: workload records carry a PID start time"
# Signalling a stored PID without verifying identity can kill a process that
# inherited the recycled PID.
# ==============================================================================
"$TARGET" start --json --id pidchk "$SLEEPER" 60 >/dev/null 2>&1
sleep 0.4
START_FIELD=$("$TARGET" inspect --json pidchk 2>/dev/null | python3 -c "
import json,sys,os
d=json.load(sys.stdin)
p=f\"{d['log_file'].rsplit('/',1)[0]}/state.json\"
print(json.load(open(p)).get('pid_start_time'))
" 2>/dev/null)
if [ -n "$START_FIELD" ] && [ "$START_FIELD" != "0" ] && [ "$START_FIELD" != "None" ]; then
    pass "PID start time recorded at launch (pid_start_time=$START_FIELD)"
else
    fail "PID start time recorded" "got '$START_FIELD'"
fi
"$TARGET" stop --force pidchk >/dev/null 2>&1
"$TARGET" delete --force pidchk >/dev/null 2>&1

# ==============================================================================
info "Test 12: runtime hardening blocks dangerous syscalls inside workloads"
# ==============================================================================
PROBE_SRC="$(mktemp /tmp/minibox-probe-XXXXXX.c)"
PROBE_BIN="${PROBE_SRC%.c}"
cat > "$PROBE_SRC" <<'PROBE_EOF'
#include <errno.h>
#include <stdio.h>
#include <sys/syscall.h>
#include <unistd.h>
int main(void) {
    errno = 0;
    long r = syscall(SYS_ptrace, 0, 0, 0, 0);
    printf("ptrace_errno=%d\n", errno);
    errno = 0;
    r = syscall(SYS_perf_event_open, 0, 0, -1, -1, 0);
    printf("perf_errno=%d\n", errno);
    return 0;
}
PROBE_EOF
if cc -O0 -o "$PROBE_BIN" "$PROBE_SRC" 2>/dev/null; then
    HOST_PTRACE=$("$PROBE_BIN" | grep '^ptrace_errno=' | cut -d= -f2)
    "$TARGET" start --json --id seccmp "$PROBE_BIN" >/dev/null 2>&1
    sleep 0.4
    OUT=$("$TARGET" logs seccmp 2>/dev/null)
    BOX_PTRACE=$(echo "$OUT" | grep '^ptrace_errno=' | cut -d= -f2)
    BOX_PERF=$(echo "$OUT" | grep '^perf_errno=' | cut -d= -f2)
    if [ "$BOX_PTRACE" = "1" ] && [ "$BOX_PERF" = "1" ]; then
        pass "seccomp denies ptrace/perf_event_open inside workloads (host ptrace errno=$HOST_PTRACE)"
    else
        fail "seccomp filtering" "workload ptrace_errno='$BOX_PTRACE' perf_errno='$BOX_PERF'"
    fi
    "$TARGET" stop --force seccmp >/dev/null 2>&1
    "$TARGET" delete --force seccmp >/dev/null 2>&1
else
    fail "seccomp probe build" "could not compile probe"
fi
rm -f "$PROBE_SRC" "$PROBE_BIN"

# ==============================================================================
info "Test 13: isolation and resource limits still work end to end"
# Regression guard for the hardening changes.
# ==============================================================================
if "$TARGET" run --json --id isoregress --isolate --hostname regress-box --pid-ns \
        "$REPO_ROOT/bin/examples/check_isolation" >/dev/null 2>&1; then
    pass "Full isolation still starts successfully with hardening enabled"
else
    fail "Full isolation with hardening" "workload failed to run"
fi
if "$TARGET" run --json --id limregress --cpu 50 --memory 64M --pids 8 "$HELLO" >/dev/null 2>&1; then
    pass "Resource limits still apply with hardening enabled"
else
    fail "Limits with hardening" "limited workload failed to run"
fi
FORK_RC=$("$TARGET" run --json --id forkregress -- "$REPO_ROOT/bin/examples/fork_test" >/dev/null 2>&1; echo $?)
if [ "$FORK_RC" = "2" ]; then
    pass "fork_test unaffected by seccomp filter (exit 2, unconstrained)"
else
    fail "fork_test under seccomp" "exit $FORK_RC, expected 2"
fi

# ==============================================================================
echo ""
echo "Regression Test Results: $PASSED passed, $FAILED failed"
[ "$FAILED" -eq 0 ] || exit 1
exit 0
