#!/usr/bin/env bash
# Cross-compile TinyPro5 for the Ableton Move (AArch64 Cortex-A72, glibc) and package TinyPro5.tar.gz.
#
# Toolchain auto-detection (first match wins):
#   1. CROSS_PREFIX=aarch64-linux-gnu-   (GCC/Clang cross toolchain; ${CROSS_PREFIX}gcc)
#   2. aarch64-linux-gnu-gcc on PATH
#   3. zig cc -target aarch64-linux-gnu.2.28   (no sysroot needed; works on Windows/macOS/Linux)
#   4. Docker (debian + gcc-aarch64-linux-gnu)
set -euo pipefail
cd "$(dirname "$0")/.."

ID=tinypro5
OUT=dist/$ID
GLIBC=${GLIBC:-2.28}

# --- pick a cross compiler -------------------------------------------------
if [ -n "${CROSS_PREFIX:-}" ] && command -v "${CROSS_PREFIX}gcc" >/dev/null 2>&1; then
  ARM_CC=("${CROSS_PREFIX}gcc"); ARM_FLAGS=(-mcpu=cortex-a72)
elif command -v aarch64-linux-gnu-gcc >/dev/null 2>&1; then
  ARM_CC=(aarch64-linux-gnu-gcc); ARM_FLAGS=(-mcpu=cortex-a72)
elif command -v zig >/dev/null 2>&1; then
  ARM_CC=(zig cc -target "aarch64-linux-gnu.${GLIBC}"); ARM_FLAGS=(-mcpu=cortex_a72)
elif command -v docker >/dev/null 2>&1; then
  echo "== no local cross compiler, building in Docker"
  exec docker run --rm -v "$PWD":/work -w /work debian:bookworm bash -c \
    'apt-get update -qq && apt-get install -y -qq gcc-aarch64-linux-gnu make python3 tar gcc >/dev/null && CROSS_PREFIX=aarch64-linux-gnu- scripts/build.sh'
else
  echo "no AArch64 cross compiler found (install gcc-aarch64-linux-gnu, zig, or Docker)" >&2; exit 2
fi
echo "== cross compiler: ${ARM_CC[*]} ${ARM_FLAGS[*]}"

# --- native compiler for the module.json generator -------------------------
if [ -n "${CC:-}" ]; then read -r -a NCC <<<"$CC"
elif command -v gcc >/dev/null 2>&1; then NCC=(gcc)
elif command -v clang >/dev/null 2>&1; then NCC=(clang)
else NCC=(zig cc); fi

PY=""
for cand in python3 python py; do
  if command -v "$cand" >/dev/null 2>&1 && "$cand" -c 'import sys; sys.exit(0)' >/dev/null 2>&1; then PY=$cand; break; fi
done
[ -n "$PY" ] || { echo "a working python3 is required for the dsp.so audit" >&2; exit 2; }

rm -rf dist build/arm
mkdir -p "$OUT" build/arm

SRCS=(src/tinypro5.c src/presets_factory.c src/bank.c src/overlay.c src/h2p.c src/bankscan.c src/meta.c src/plugin.c)

# --- DSP shared object ------------------------------------------------------
echo "== compile dsp.so  (-std=c11 -Wall -Wextra -Werror -pedantic -O3)"
"${ARM_CC[@]}" "${ARM_FLAGS[@]}" -std=c11 -Wall -Wextra -Werror -pedantic \
    -O3 -fPIC -shared -fvisibility=hidden -fno-math-errno -ffunction-sections -fdata-sections \
    -Wl,--gc-sections -Wl,-z,relro -Wl,-z,now \
    -o build/arm/dsp-unstripped.so "${SRCS[@]}" -lm
"${ARM_CC[@]}" "${ARM_FLAGS[@]}" -std=c11 -Wall -Wextra -Werror -pedantic \
    -O3 -fPIC -shared -fvisibility=hidden -fno-math-errno -ffunction-sections -fdata-sections \
    -Wl,--gc-sections -Wl,-z,relro -Wl,-z,now -s \
    -o "$OUT/dsp.so" "${SRCS[@]}" -lm

# --- module.json generated from the parameter table ------------------------
echo "== generate module.json"
GEN=build/gen_module_json; case "$(uname -s)" in MINGW*|MSYS*|CYGWIN*) GEN=build/gen_module_json.exe ;; esac
"${NCC[@]}" -std=c11 -Wall -Wextra -Werror -pedantic -O1 -o "$GEN" tools/gen_module_json.c src/meta.c src/tinypro5.c src/presets_factory.c src/bank.c src/overlay.c -lm
"./$GEN" "$OUT/module.json"

cp src/ui.js "$OUT/ui.js"
cp src/canvas.js "$OUT/canvas.js"   # the Reload / Save / Revert confirm screens (module.json canvas_script)
mkdir -p "$OUT/banks"      # empty on purpose: the README below is the only file the module ships there (users add their own banks)
cp LICENSE "$OUT/"
cp release.json "$OUT/release.json"      # the update check reads it from the module folder and from the repository root
cat > "$OUT/banks/README.txt" <<'BANKS_EOF'
TinyPro5 custom banks
=====================
Drop sound banks here and restart / reload the module; they are found once, when the module is created.

  Name.zip    every .h2p (u-he Repro-5 patches) and .syx (Prophet-5 .syx program dumps) inside becomes the bank "Name"
  Name.syx     one Prophet-5 program dump file = one bank
  Name/        an extracted bank folder (up to 3 folder levels) = one bank
  *.h2p        loose Repro-5 patches = the bank "Loose"

Page 4, encoder 8 (Bank) switches banks: bank 1 is always the 32 built-in factory programs. A new bank loads its first
program. Jog click toggles bank order / A-Z. Programs from other instruments (e.g. Repro-1 .h2p files) are skipped.
Save works on every bank. Edits are stored in ../user/<bank>.tp5u; the bank files here are never modified.

Third-party bank files stay on your device; keep them out of version control.
BANKS_EOF
cp README.md "$OUT/README.md"
mkdir -p "$OUT/docs"
cp docs/help.json "$OUT/help.json"      # the on-device help system reads help.json from the module folder
cp docs/user_guide.md docs/release_notes.md "$OUT/docs/"

# --- verify the artefact ----------------------------------------------------
echo "== audit dsp.so"
"$PY" scripts/check_symbols.py "$OUT/dsp.so"

# --- package ----------------------------------------------------------------
echo "== package"
rm -f TinyPro5.tar.gz
tar --owner=0 --group=0 --numeric-owner -czf TinyPro5.tar.gz -C dist "$ID"
tar -tzf TinyPro5.tar.gz

echo
echo "== sizes"
ls -l "$OUT" TinyPro5.tar.gz | awk 'NF>=9 {printf "  %10d  %s\n", $5, $9}'
printf '  %10d  dsp.so (unstripped, with symbols)\n' "$(wc -c < build/arm/dsp-unstripped.so)"
echo
echo "built TinyPro5.tar.gz"
