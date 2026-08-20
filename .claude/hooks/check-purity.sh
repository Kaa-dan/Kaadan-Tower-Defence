#!/bin/bash
# Runs the game/ invariant guards the moment a simulation file changes.
#
# They take ~0.16s, so the feedback is effectively instant, and catching a
# violated invariant at the edit that introduced it is far cheaper than finding
# it at PR time -- or worse, discovering months later that game/ quietly grew a
# platform dependency.
#
# Non-blocking by design: it reports on stderr and exits 0. The guards also run
# in ctest and in CI, which are the real gates. A hook that blocks edits would
# make it impossible to do a multi-file change that is only valid once complete.

set -uo pipefail

INPUT=$(cat)
FILE=$(printf '%s' "$INPUT" | sed -n 's/.*"file_path"[[:space:]]*:[[:space:]]*"\([^"]*\)".*/\1/p')

# Only care about the simulation core.
case "$FILE" in
    *"/game/"*) ;;
    *) exit 0 ;;
esac

PROJECT_DIR="${CLAUDE_PROJECT_DIR:-$(git rev-parse --show-toplevel 2>/dev/null)}"
[ -d "$PROJECT_DIR/game" ] || exit 0

FAILED=""
for RULE in platform determinism stdlib; do
    # -DENFORCE_CMATH=ON to match tests/CMakeLists.txt. If the hook were more
    # lenient than CTest, the failure would just move from the edit to CI.
    if ! OUTPUT=$(cmake -DGAME_DIR="$PROJECT_DIR/game" -DRULE="$RULE" \
                        -DENFORCE_CMATH=ON \
                        -P "$PROJECT_DIR/tests/check_purity.cmake" 2>&1); then
        FAILED="yes"
        echo "purity/$RULE FAILED after editing $FILE" >&2
        echo "$OUTPUT" | grep -E '^--' >&2
    fi
done

if [ -n "$FAILED" ]; then
    echo "" >&2
    echo "game/ must stay pure and deterministic. See .claude/rules/game-core.md" >&2
fi

exit 0
