#!/usr/bin/env bash
# Build and run the native unit tests (tests/test_prophet5.c) with the production warning flags.
# Compiler: $CC if set, else gcc / clang / `zig cc`.
set -euo pipefail
cd "$(dirname "$0")/.."

if [ -n "${CC:-}" ]; then read -r -a CCMD <<<"$CC"
elif command -v gcc >/dev/null 2>&1; then CCMD=(gcc)
elif command -v clang >/dev/null 2>&1; then CCMD=(clang)
elif command -v zig >/dev/null 2>&1; then CCMD=(zig cc)
else echo "no C compiler found (set CC=...)" >&2; exit 2; fi
echo "== compiler: ${CCMD[*]}"

mkdir -p build
EXE=build/test_prophet5
case "$(uname -s)" in MINGW*|MSYS*|CYGWIN*) EXE=build/test_prophet5.exe ;; esac

SRCS=(src/tinypro5.c src/presets_factory.c src/bank.c src/overlay.c src/h2p.c src/bankscan.c src/meta.c src/plugin.c)
# bankscan.c is the module's only file IO (run once from create_instance, never while rendering): everything else is audited
AUDIT=(src/tinypro5.c src/presets_factory.c src/bank.c src/overlay.c src/h2p.c src/meta.c src/plugin.c)

echo "== real-time source audit (no heap / file IO / locking in the engine)"
if grep -nE '\b(malloc|calloc|realloc|free|aligned_alloc|posix_memalign|strdup|fopen|fprintf|printf|puts|fwrite|fread|pthread_[a-z_]+)[[:space:]]*\(' "${AUDIT[@]}" src/*.h; then
  echo "FAIL: forbidden call found in the DSP sources" >&2; exit 1
fi
echo "   clean"

echo "== compile (-std=c11 -Wall -Wextra -Werror -pedantic)"
"${CCMD[@]}" -std=c11 -Wall -Wextra -Werror -pedantic -O2 -g -o "$EXE" tests/test_prophet5.c "${SRCS[@]}" -lm

echo "== run"
"./$EXE"

echo "== factory-bank audit (32 original programs: levels, ceiling, structure)"
AU=build/test_audit
case "$(uname -s)" in MINGW*|MSYS*|CYGWIN*) AU=build/test_audit.exe ;; esac
"${CCMD[@]}" -std=c11 -Wall -Wextra -Werror -pedantic -O2 -o "$AU" tests/test_audit.c src/tinypro5.c src/presets_factory.c src/bank.c src/overlay.c src/h2p.c src/bankscan.c -lm
"./$AU"

echo "== custom banks (zip / inflate / .h2p / .syx / registry / folder scan)"
BK=build/test_banks
case "$(uname -s)" in MINGW*|MSYS*|CYGWIN*) BK=build/test_banks.exe ;; esac
"${CCMD[@]}" -std=c11 -Wall -Wextra -Werror -pedantic -O2 -o "$BK" tests/test_banks.c src/tinypro5.c src/presets_factory.c src/bank.c src/overlay.c src/h2p.c src/bankscan.c src/meta.c -lm
"./$BK"

# Optional second pass under UBSan when the toolchain supports it.
if [ "${SANITIZE:-0}" = "1" ]; then
  echo "== UBSan pass"
  "${CCMD[@]}" -std=c11 -Wall -Wextra -Werror -pedantic -O1 -g -fsanitize=undefined -fno-sanitize-recover=undefined \
      -o "${EXE%.exe}_ubsan${EXE##*test_prophet5}" tests/test_prophet5.c "${SRCS[@]}" -lm
  "./${EXE%.exe}_ubsan${EXE##*test_prophet5}"
  "${CCMD[@]}" -std=c11 -Wall -Wextra -Werror -pedantic -O1 -g -fsanitize=undefined -fno-sanitize-recover=undefined       -o build/test_banks_ubsan tests/test_banks.c src/tinypro5.c src/presets_factory.c src/bank.c src/overlay.c src/h2p.c src/bankscan.c src/meta.c -lm
  ./build/test_banks_ubsan
  "${CCMD[@]}" -std=c11 -Wall -Wextra -Werror -pedantic -O1 -g -fsanitize=undefined -fno-sanitize-recover=undefined       -o build/test_audit_ubsan tests/test_audit.c src/tinypro5.c src/presets_factory.c src/bank.c src/overlay.c src/h2p.c src/bankscan.c -lm
  ./build/test_audit_ubsan
fi

# The confirm screens (canvas.js) driven with a fake host context (needs node; skipped without it).
if command -v node >/dev/null 2>&1; then
  echo "== canvas.js confirm screens"
  node tests/test_canvas.js
fi

# In-memory preset verification through ctypes (skipped when no working Python is installed).
PY=""
for cand in python3 python py; do
  if command -v "$cand" >/dev/null 2>&1 && "$cand" -c 'import sys; sys.exit(0)' >/dev/null 2>&1; then PY=$cand; break; fi
done
if [ -n "$PY" ]; then
  echo "== ctypes preset verification"
  ./scripts/build_testlib.sh
  "$PY" -I tools/verify_presets.py
else
  echo "== (python not found: skipping ctypes preset verification)"
fi
