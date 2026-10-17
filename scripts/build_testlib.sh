#!/usr/bin/env bash
# Build build/libtinypro5_test.so: the host-native engine with a flat C ABI for Python ctypes.
# (On Windows hosts the file is a PE DLL carrying the .so name; ctypes loads it either way.)
set -euo pipefail
cd "$(dirname "$0")/.."

if [ -n "${CC:-}" ]; then read -r -a CCMD <<<"$CC"
elif command -v gcc >/dev/null 2>&1; then CCMD=(gcc)
elif command -v clang >/dev/null 2>&1; then CCMD=(clang)
elif command -v zig >/dev/null 2>&1; then CCMD=(zig cc)
else echo "no C compiler found (set CC=...)" >&2; exit 2; fi

mkdir -p build
"${CCMD[@]}" -std=c11 -Wall -Wextra -Werror -pedantic -O2 -fPIC -shared \
    -o build/libtinypro5_test.so tools/tp5_ctypes.c src/tinypro5.c src/presets_factory.c src/bank.c src/overlay.c src/h2p.c src/bankscan.c -lm
echo "built build/libtinypro5_test.so"
