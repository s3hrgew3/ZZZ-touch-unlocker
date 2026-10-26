#!/usr/bin/env bash
# =============================================================================
# zzz-touch-selftest.sh — end-to-end smoke test for the Wine port.
#
# It needs no game: a synthetic "ZenlessZoneZero.exe" (compiled here) plus a
# throw-away wine prefix stand in for the real thing. What is verified:
#
#   * prefix hygiene   clean resolves to "no clients", status reports it
#   * launch ordering  the wrapper starts the game itself and waits for it
#   * attach injection second client injects zzz-touch-hook.dll successfully
#   * mode plumbing    zzz-touch-mode.txt reaches the hook (modeSource=FILE)
#   * logging          hook log lands in the requested directory
#   * clean            kills the synthetic game, prefix returns to clean
#   * dry-run          prints the commands it would run, changes nothing
#
# The hook itself is expected to end in NTR_ABORT on the dummy target: the dummy
# has no GameAssembly.dll, so the pattern locators fail. That abort is the proof
# the DLL really loaded and executed inside the target process.
#
# Usage:
#   wine/zzz-touch-selftest.sh [-m 0|1] [--keep] [--runner DIR] [--prefix DIR]
#                              [--grace-ms N]
#
#   --keep        keep the temporary directory (path is printed)
#   --prefix DIR  reuse an existing prefix instead of creating one
#                 (it is only used, never cleaned by the test setup)
#   --runner DIR  wine runner; default: taken from the launcher config
#
# Exit codes: 0 pass, 1 fail, 2 missing prerequisites.
# =============================================================================
set -uo pipefail

SELF_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
# Works both in-tree (wine/) and staged (dist/wine/): pick the first directory
# that actually holds the built artifacts.
DIST=""
for candidate in "$SELF_DIR" "$SELF_DIR/../dist/wine" "$SELF_DIR/../../dist/wine"; do
    if [ -f "$candidate/zzz-touch-hook.dll" ]; then DIST=$candidate; break; fi
done
[ -n "$DIST" ] || DIST=$SELF_DIR
ROOT=$(cd "$DIST/.." && pwd)

MODE=1
KEEP=0
RUNNER=${ZZZ_RUNNER:-}
PREFIX=""
GRACE_MS=1500
FAILED=0

say() { printf '%s\n' "$*"; }
ok() { printf '  \033[32mPASS\033[0m %s\n' "$*"; }
bad() { printf '  \033[31mFAIL\033[0m %s\n' "$*"; FAILED=1; }
note() { printf '  ---- %s\n' "$*"; }

while [ $# -gt 0 ]; do
    case "$1" in
        -m|--mode) MODE=$2; shift ;;
        --keep) KEEP=1 ;;
        --runner) RUNNER=$2; shift ;;
        --prefix) PREFIX=$2; shift ;;
        --grace-ms) GRACE_MS=$2; shift ;;
        -h|--help) sed -n '2,30p' "${BASH_SOURCE[0]}"; exit 0 ;;
        *) echo "unknown option: $1" >&2; exit 2 ;;
    esac
    shift
done

# ---------------------------------------------------------------------------
# prerequisites
# ---------------------------------------------------------------------------
for bin in x86_64-w64-mingw32-gcc cmake; do
    command -v "$bin" >/dev/null 2>&1 || { echo "missing prerequisite: $bin" >&2; exit 2; }
done
for file in zzz-touch-hook.dll zzz-touch-inject.exe zzz-touch-unlocker; do
    [ -f "$DIST/$file" ] || {
        echo "missing $DIST/$file — run scripts/build-wine.sh first" >&2
        exit 2
    }
done

TMP=$(mktemp -d "${TMPDIR:-/tmp}/zzz-selftest.XXXXXX") || exit 2
cleanup() {
    if [ -n "$GAME_PID" ] && kill -0 "$GAME_PID" 2>/dev/null; then
        kill "$GAME_PID" 2>/dev/null
    fi
    if [ "$KEEP" = 1 ]; then
        echo "kept: $TMP"
    else
        rm -rf "$TMP"
    fi
}
trap cleanup EXIT
GAME_PID=""

if [ -z "$RUNNER" ]; then
    RUNNER=$(python3 - "$ROOT" <<'PY' 2>/dev/null
import json, os, sys
root = sys.argv[1]
for path in (os.path.join(root, "config.json"),
             os.path.expanduser("~/.local/share/sleepy-launcher/config.json")):
    if not os.path.isfile(path):
        continue
    try:
        d = json.load(open(path, encoding="utf-8"))
    except Exception:
        continue
    wine = d.get("game", {}).get("wine", {})
    builds, selected = wine.get("builds"), wine.get("selected")
    if builds and selected:
        print(os.path.join(builds, selected))
        break
PY
)
fi
WINE="$RUNNER/bin/wine"
[ -x "$WINE" ] || { echo "wine runner not found (--runner): $RUNNER" >&2; exit 2; }
say "runner : $RUNNER"
say "workdir: $TMP"

# ---------------------------------------------------------------------------
# synthetic target + staging
# ---------------------------------------------------------------------------
mkdir -p "$TMP/game" "$TMP/dist" "$TMP/logs"
cat >"$TMP/dummy.c" <<'EOF'
#include <windows.h>
#include <stdio.h>
int main(void) {
    printf("selftest target up (pid=%lu)\n", (unsigned long)GetCurrentProcessId());
    fflush(stdout);
    for (;;) Sleep(1000);
    return 0;
}
EOF
x86_64-w64-mingw32-gcc -O1 -o "$TMP/game/ZenlessZoneZero.exe" "$TMP/dummy.c" || {
    echo "cannot compile the synthetic target" >&2; exit 2;
}
install -m 0644 "$DIST/zzz-touch-hook.dll" "$TMP/dist/zzz-touch-hook.dll"
install -m 0755 "$DIST/zzz-touch-inject.exe" "$TMP/dist/zzz-touch-inject.exe"
install -m 0755 "$DIST/zzz-touch-unlocker" "$TMP/dist/zzz-touch-unlocker"

PREFIX_ARGS=()
if [ -n "$PREFIX" ]; then
    PREFIX_ARGS=(--prefix "$PREFIX")
    say "prefix : $PREFIX (provided)"
else
    PREFIX="$TMP/prefix"
    say "prefix : $PREFIX (creating)"
    WINEPREFIX="$PREFIX" "$WINE" wineboot -u >"$TMP/wineboot.log" 2>&1 || {
        echo "wineboot failed, see $TMP/wineboot.log" >&2; exit 2;
    }
fi

WRAPPER=("$TMP/dist/zzz-touch-unlocker")
COMMON=(--game "$TMP/game"
        --prefix "$PREFIX"
        --runner "$RUNNER"
        --dll "$TMP/dist/zzz-touch-hook.dll"
        --inject-exe "$TMP/dist/zzz-touch-inject.exe"
        --log-dir "$TMP/logs"
        --grace-ms "$GRACE_MS"
        --wait-ms 30000)

# The dummy target has no Unity locators, so the hook must end in NTR_ABORT.
# Shorten its locator budget (the game process inherits our environment).
export ZZZ_TOUCH_FINALIZE_TIMEOUT_MS=3000

# ---------------------------------------------------------------------------
# 0. wineboot leaves a hot prefix: clean must bring it back to zero
# ---------------------------------------------------------------------------
say ""
say "[0] clean after wineboot"
out=$("${WRAPPER[@]}" clean "${COMMON[@]}" 2>&1); rc=$?
[ $rc -eq 0 ] && ok "clean exits 0 on a freshly booted prefix" \
    || { bad "clean exits $rc"; note "$out"; }
grep -q "prefix clean" <<<"$out" && ok "wineboot services terminated" \
    || { bad "wineboot services survived clean"; note "$out"; }

# ---------------------------------------------------------------------------
# 1. status before anything runs
# ---------------------------------------------------------------------------
say "[1] status on a cold prefix"
out=$("${WRAPPER[@]}" status "${COMMON[@]}" 2>&1); rc=$?
[ $rc -eq 0 ] && ok "status exits 0" || bad "status exits $rc"
grep -q "prefix clients   : none" <<<"$out" && ok "prefix reported clean" \
    || { bad "prefix not reported clean"; note "$out"; }
grep -q "dll    = $TMP/dist/zzz-touch-hook.dll (present)" <<<"$out" \
    && ok "DLL detected" || bad "DLL not detected"

# ---------------------------------------------------------------------------
# 2. dry-run must not start anything
# ---------------------------------------------------------------------------
say "[2] --dry-run"
out=$("${WRAPPER[@]}" run --dry-run "${COMMON[@]}" 2>&1); rc=$?
[ $rc -eq 0 ] && ok "dry-run exits 0" || bad "dry-run exits $rc"
grep -q "would run: cd $TMP/game" <<<"$out" && ok "dry-run prints the launch command" \
    || { bad "dry-run launch line missing"; note "$out"; }
[ -z "$(pgrep -f "$TMP/game/ZenlessZoneZero.exe" 2>/dev/null)" ] \
    && ok "dry-run started nothing" || bad "dry-run started the game"

# ---------------------------------------------------------------------------
# 3. real run: clean -> launch -> grace -> inject
# ---------------------------------------------------------------------------
say "[3] run (launch + attach)"
out=$("${WRAPPER[@]}" run "${COMMON[@]}" 2>&1); rc=$?
note "$(tr '\n' '|' <<<"$out")"
[ $rc -eq 0 ] && ok "run exits 0" || bad "run exits $rc"
grep -q "prefix clean" <<<"$out" && ok "prefix cleaned before launch" \
    || bad "prefix was not cleaned"
grep -q "anti-cheat stage cleared" <<<"$out" && ok "attach was deferred to the guard" \
    || bad "no guard line"
grep -q "injected OK" <<<"$out" && ok "injector reported success" \
    || bad "injector did not report success"

# the synthetic target must still be alive: injection must not kill it.
# grep /proc/*/maps instead of pgrep -f (wine keeps comm at "steam.exe" and the
# game lives in a forked child, so process names are not a reliable handle).
GAME_PID=""
for maps in /proc/[0-9]*/maps; do
    [ -r "$maps" ] || continue
    grep -qF "$TMP/game/ZenlessZoneZero.exe" "$maps" 2>/dev/null || continue
    GAME_PID=${maps#/proc/}; GAME_PID=${GAME_PID%/maps}
    break
done
[ -n "$GAME_PID" ] && ok "target alive after injection (pid $GAME_PID)" \
    || bad "target died during injection"

# mode file + hook log
[ "$(cat "$TMP/dist/zzz-touch-mode.txt" 2>/dev/null)" = "$MODE" ] \
    && ok "mode file = $MODE" || bad "mode file missing/wrong"
HOOK_LOG=$(ls -t "$TMP"/logs/logs/touch-*.log 2>/dev/null | head -1)
if [ -n "$HOOK_LOG" ]; then
    ok "hook log written: ${HOOK_LOG#$TMP/}"
    grep -q "NTR_HOST wine=" "$HOOK_LOG" && ok "hook detected wine" || bad "NTR_HOST missing"
    grep -q "modeSource=FILE" "$HOOK_LOG" && ok "mode came from the mode file" \
        || bad "modeSource is not FILE"
    grep -q "NTR_MODE mode=$MODE" "$HOOK_LOG" && ok "hook applied mode $MODE" \
        || bad "hook did not apply mode $MODE"
    # let the hook hit its (shortened) locator timeout, then require a verdict
    verdict=""
    for _ in $(seq 1 100); do
        if grep -q "NTR_ABORT\|NTR_READY" "$HOOK_LOG" 2>/dev/null; then verdict=1; break; fi
        sleep 0.2
    done
    grep -q "NTR_ABORT" "$HOOK_LOG" \
        && ok "hook aborted on the dummy target (expected: no GameAssembly)" \
        || bad "hook neither armed nor aborted"
    grep -q "NTR_READY" "$HOOK_LOG" && bad "unexpected NTR_READY on the dummy target"
else
    bad "no hook log under $TMP/logs/logs"
fi

# second client must not have broken the prefix view
out=$("${WRAPPER[@]}" status "${COMMON[@]}" 2>&1)
grep -q "running game pids: .*\b$GAME_PID\b" <<<"$out" && ok "status sees the running target" \
    || { bad "status does not see the running target"; note "$out"; }

# ---------------------------------------------------------------------------
# 4. clean must kill the synthetic game
# ---------------------------------------------------------------------------
say "[4] clean"
out=$("${WRAPPER[@]}" clean "${COMMON[@]}" 2>&1); rc=$?
[ $rc -eq 0 ] && ok "clean exits 0" || { bad "clean exits $rc"; note "$out"; }
sleep 1
survivors=""
for maps in /proc/[0-9]*/maps; do
    [ -r "$maps" ] || continue
    grep -qF "$TMP/game/ZenlessZoneZero.exe" "$maps" 2>/dev/null && survivors=$((survivors+1))
done
[ "${survivors:-0}" -eq 0 ] && ok "target gone after clean" \
    || bad "target survived clean ($survivors processes)"
GAME_PID=""
grep -q "prefix clean" <<<"$out" && ok "prefix reported clean after clean" \
    || bad "clean did not confirm an empty prefix"

# ---------------------------------------------------------------------------
say ""
if [ "$FAILED" = 0 ]; then
    printf '\033[32mselftest PASSED\033[0m\n'
    exit 0
fi
printf '\033[31mselftest FAILED\033[0m\n'
[ "$KEEP" = 1 ] || echo "re-run with --keep to inspect $TMP"
exit 1
