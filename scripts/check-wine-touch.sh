#!/usr/bin/env bash
# check-wine-touch -- does this Wine runner deliver WM_TOUCH?
#
# The touch-unlocker needs a Wine build whose win32u implements the touch
# window API. Downstream builds differ wildly. Measured on this machine:
#
#   spritz-wine-cachyos-wow64-10.0-11        PASS  (WM_TOUCH arrives)
#   spritz-wine-tkg-staging-wow64-11.19-1    FAIL  (RegisterTouchWindow/IsTouchWindow
#                                                  are fixme stubs, NtUserIsTouchWindow
#                                                  aborts the process)
#   wine-dwproton / wine-staging-tkg / soda  not installed here, unverified
#
# This script answers the question for one runner.
#
#   scripts/check-wine-touch.sh                       # runner from the launcher config
#   scripts/check-wine-touch.sh --runner DIR          # explicit runner root
#   scripts/check-wine-touch.sh --manual              # touch it yourself instead of uinput
#   scripts/check-wine-touch.sh --list                # show the installable runners
#
# Exit codes: 0 touch works, 1 touch does not work, 2 usage/setup problem.
set -uo pipefail

SELF_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
REPO=$(cd -- "$SELF_DIR/.." && pwd)

LAUNCHER_DIR=${SLEEPY_DIR:-$HOME/.local/share/sleepy-launcher}
CONFIG=${ZZZ_CONFIG:-$LAUNCHER_DIR/config.json}
CACHE=${XDG_CACHE_HOME:-$HOME/.cache}/zzz-touch-probe
RUNNER=${ZZZ_RUNNER:-}
SECONDS_=${SECONDS_:-10}
MANUAL=0
KEEP=0
LIST=0

die() { printf 'check-wine-touch: %s\n' "$*" >&2; exit 2; }
say() { printf '\n== %s\n' "$*"; }

usage() {
    sed -n '2,20p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'
}

while [ $# -gt 0 ]; do
    case "$1" in
        --runner) RUNNER=$2; shift 2 ;;
        --runner=*) RUNNER=${1#*=}; shift ;;
        --seconds) SECONDS_=$2; shift 2 ;;
        --manual) MANUAL=1; shift ;;
        --keep) KEEP=1; shift ;;
        --list) LIST=1; shift ;;
        -h|--help) usage; exit 0 ;;
        *) die "unknown argument: $1" ;;
    esac
done

# ---------------------------------------------------------------------------
# runner list / config lookup
# ---------------------------------------------------------------------------
COMPONENTS=$LAUNCHER_DIR/components/wine

if [ "$LIST" = 1 ]; then
    [ -d "$COMPONENTS" ] || die "component list not found: $COMPONENTS"
    python3 - "$COMPONENTS" "$LAUNCHER_DIR/runners" <<'PY'
import json, glob, os, sys
comp, runners = sys.argv[1], sys.argv[2]
for f in sorted(glob.glob(os.path.join(comp, '*.json'))):
    entries = json.load(open(f))
    print(f"[{os.path.basename(f)[:-5]}]")
    for e in entries:
        installed = os.path.isdir(os.path.join(runners, e['name']))
        mark = '*' if installed else ' '
        print(f"  {mark} {e['name']:45s} {e['title']}")
print("\n* = installed under " + runners)
PY
    exit 0
fi

if [ -z "$RUNNER" ]; then
    [ -f "$CONFIG" ] || die "launcher config not found: $CONFIG (use --runner)"
    RUNNER=$(python3 - "$CONFIG" "$LAUNCHER_DIR" <<'PY'
import json, os, sys
cfg, base = sys.argv[1], sys.argv[2]
d = json.load(open(cfg))
wine = d.get('game', {}).get('wine', {})
sel = wine.get('selected')
builds = wine.get('builds') or os.path.join(base, 'runners')
print(os.path.join(builds, sel) if sel else "")
PY
)
fi
[ -n "$RUNNER" ] || die "no runner selected (use --runner DIR)"
[ -x "$RUNNER/bin/wine" ] || die "not a wine runner: $RUNNER"

WINE=$RUNNER/bin/wine
WINEPREFIX=${ZZZ_PROBE_PREFIX:-$CACHE/prefix}
PROBE_EXE=$CACHE/touch-probe.exe
TAP_BIN=$CACHE/uinput-tap

say "wine runner"
echo "  runner = $RUNNER"
echo "  prefix = $WINEPREFIX"

# ---------------------------------------------------------------------------
# build the helpers when needed
# ---------------------------------------------------------------------------
mkdir -p "$CACHE" || die "cannot create $CACHE"

if [ ! -x "$PROBE_EXE" ] || [ "$REPO/wine/touch-probe.c" -nt "$PROBE_EXE" ]; then
    command -v x86_64-w64-mingw32-gcc >/dev/null || die "x86_64-w64-mingw32-gcc missing"
    x86_64-w64-mingw32-gcc -O2 -o "$PROBE_EXE" "$REPO/wine/touch-probe.c" -luser32 -lgdi32 \
        || die "failed to build touch-probe.exe"
fi
if [ "$MANUAL" = 0 ]; then
    if [ ! -x "$TAP_BIN" ] || [ "$REPO/wine/uinput-tap.c" -nt "$TAP_BIN" ]; then
        gcc -O2 -o "$TAP_BIN" "$REPO/wine/uinput-tap.c" || die "failed to build uinput-tap"
    fi
    [ -w /dev/uinput ] || die "/dev/uinput not writable; rerun with --manual"
fi

# ---------------------------------------------------------------------------
# run the probe
# ---------------------------------------------------------------------------
LOG=$CACHE/probe.log
: >"$LOG"

export WINEPREFIX
"$WINE" "$PROBE_EXE" --seconds "$SECONDS_" >"$LOG" 2>&1 &
WPID=$!

# wait for the window to be up before injecting
listening=0
for _ in $(seq 1 300); do
    if grep -q "listening" "$LOG" 2>/dev/null; then listening=1; break; fi
    kill -0 "$WPID" 2>/dev/null || break
    sleep 0.1
done
if [ "$listening" = 0 ]; then
    kill "$WPID" 2>/dev/null
    wait "$WPID" 2>/dev/null
    say "probe did not come up"
    grep -a "RegisterTouchWindow\|IsTouchWindow\|SM_DIGITIZER" "$LOG" | sed 's/^/  /'
    [ "$KEEP" = 1 ] || rm -rf "$WINEPREFIX" 2>/dev/null
    printf '\ncheck-wine-touch: could not start the touch probe under this runner\n' >&2
    exit 2
fi

if [ "$MANUAL" = 1 ]; then
    echo
    echo "  >>> touch the black window that is on screen now (you have ${SECONDS_}s)"
else
    sleep 0.4
    "$TAP_BIN" --x 16383 --y 16383 --hold 300 >/dev/null 2>&1 \
        || die "uinput-tap failed"
    sleep 0.4
    "$TAP_BIN" --x 12000 --y 9000 --hold 200 >/dev/null 2>&1 || true
fi

wait "$WPID"
RC=$?

sed -n '/zzz-touch-probe/,$p' "$LOG" | sed 's/^/  /'

say "verdict"
if [ "$RC" = 0 ]; then
    printf '  \033[32mPASS\033[0m %s delivers WM_TOUCH\n' "$(basename "$RUNNER")"
    [ "$KEEP" = 1 ] || rm -rf "$WINEPREFIX" 2>/dev/null
    exit 0
fi
printf '  \033[31mFAIL\033[0m %s does not deliver WM_TOUCH\n' "$(basename "$RUNNER")"
echo "         RegisterTouchWindow/IsTouchWindow are most likely fixme stubs in this build;"
echo "         pick a runner that reports PASS (see --list) or test another one."
[ "$KEEP" = 1 ] || rm -rf "$WINEPREFIX" 2>/dev/null
exit 1
