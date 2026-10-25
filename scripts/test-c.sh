#!/bin/sh
# Build and run the desktop C unit tests. Uses gcc/cc if present, otherwise the
# host's gcc via host-spawn (toolbox containers have no compiler).
set -e
cd "$(dirname "$0")/.."
mkdir -p build
CC_CMD=""
for c in gcc cc; do command -v "$c" >/dev/null 2>&1 && CC_CMD="$c" && break; done
if [ -z "$CC_CMD" ]; then
    command -v host-spawn >/dev/null 2>&1 && CC_CMD="host-spawn gcc"
fi
[ -n "$CC_CMD" ] || { echo "no C compiler found (install gcc or host-spawn)" >&2; exit 1; }
$CC_CMD -std=c11 -Wall -Wextra -Werror -g \
    test/c/test_logic.c src/c/logic.c -o build/test_logic
if [ "${CC_CMD%% *}" = "host-spawn" ]; then host-spawn "$PWD/build/test_logic"; else ./build/test_logic; fi
