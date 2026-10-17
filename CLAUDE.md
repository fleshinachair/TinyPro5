# TinyPro5 - working notes for Claude

Prophet-5 style 5-voice virtual analog for the Ableton Move (Schwung). Plain C11 engine, ARM Cortex-A72 target.
See README.md for the feature list.

## Commands
- `scripts/test.sh` - native unit tests under `-std=c11 -Wall -Wextra -Werror -pedantic`, plus the ctypes preset check.
  `SANITIZE=1 scripts/test.sh` adds a UBSan pass. Must stay green before any package/deploy.
- `scripts/build_testlib.sh` - builds `build/libtinypro5_test.so` (flat C ABI in `tools/tp5_ctypes.c`) for Python/ctypes.
- `scripts/build.sh` - cross-compiles `dsp.so` for aarch64 (zig cc), audits imports, writes `TinyPro5.tar.gz`.
- Compiler here is `zig cc`; there is no gcc or make. This directory is a git repository (origin = github.com/fleshinachair/TinyPro5). Python is `python` (3.10); use `python -I` for anything that reads untrusted files.

## Layout
- `src/bank.h` + `bank.c` (registry, per-bank A-Z order), `h2p.c` (inflate/zip, .h2p, .syx parsers + mappings, pure code), `bankscan.c` (the ONLY file IO, runs once from create_instance; excluded from the source audit). Custom banks live in `<module>/banks/` and are never committed.
- `src/tinypro5.c/.h` engine (voices, SSM-style ladder, envelopes, poly-mod, LFO, state); `src/presets_factory.c` is bank 0: 32 ORIGINAL programs (4 groups x 8, labels `group-program name`), compiled in, each with a level gain (`PREG` factor x `LIFT`);
  `src/meta.c` generates module.json / chain_params / ui_hierarchy; `src/plugin.c` Schwung API v2; `src/ui.js` OLED UI.
- No third-party patch data ships with the module or belongs in git: `.syx` / `.h2p` / `.zip` banks are user-supplied, local, git-ignored.
- `tests/test_audit.c` keeps the factory bank within 4 dB for a three-note chord and under the ceiling; retune with `TP5_AUDIT_VERBOSE=1` for the full table. After editing the preset table run `scripts/test.sh`.

## Rules that matter
- No malloc/free/IO/locks anywhere in the audio path (`tp5_render*`); the `dsp.so` import audit enforces it.
- Output ceiling is -6.1 dBFS (`TINYPRO5_CEILING_LIN`), headroom constant is calibrated by test; any DSP change must keep
  every preset under it (`tools/verify_presets.py` and the C tests check this).
- Presets are not hardware-calibrated: the LFO fits for .syx import come from Melodee (see README). Never call them "hardware-calibrated".
- File-writing gotchas: the Write/Edit tools emit CRLF - run `sed -i 's/\r$//'` on scripts and C files; backslash escapes
  (`\n`, `\b`) inside Python heredocs used to patch C/JS get mangled - patch with the Edit tool or raw strings.
- Work on a branch; never force-push or rewrite history without the owner saying so.

## Device (Move)
- `ssh ableton@move.local` (key auth). Module dir: `/data/UserData/schwung/modules/sound_generators/tinypro5/`.
  Deploy by piping `TinyPro5.tar.gz` into `tar -C .../sound_generators -xzf -`, `chmod 755 dsp.so`, compare sha256.
- `/data/UserData/schwung/restart-move.sh` restarts Move/Schwung: it interrupts the user's session - only run it when asked.
- Host preset browser contract: root level declares `list_param=preset`, `count_param=preset_count`, `name_param=preset_name`;
  writing `preset` must load the program.

## Working style (from the user)
- Use only the data source the user names; do not start unrequested data-harvesting side projects.
- Report tersely and honestly, including what was not verified (the owner has auditioned the 32 original factory presets by ear and is happy with them; imported banks have not been auditioned).
