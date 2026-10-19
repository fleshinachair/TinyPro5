#!/usr/bin/env python3
"""In-memory preset verification through ctypes (no files written).

Loads build/libtinypro5_test.so (built by scripts/build_testlib.sh), then for every preset:
  * the compiled table entry loads and every listed parameter lands in the engine (clamped to range),
  * every parameter the preset does not list falls back to its default,
  * a five-note chord renders finite, audible audio and stays under the -6.1 dBFS ceiling.
  * the same chord in Rev 4 voicing (rev = 1) is finite and stays under the ceiling after the limiter,
  * the factory programs list neither rev, vintage nor velocity depths (Rev 2, vintage 0, no velocity: the engine as it was).
Exit status is non-zero on any failure.

    python -I tools/verify_presets.py                 the factory bank (what scripts/test.sh runs)
    python -I tools/verify_presets.py --banks DIR     also scan a folder of .zip / .syx / .h2p banks with the module's own loader and
                                                      render every program (3-note chord, 3 s); prints a summary
                                                      per bank (programs, Rev 4 count, vintage > 0 count) and fails on NaN / ceiling breaks
"""
import ctypes
import math
import os
import sys

try:
    import numpy as np
except ImportError:
    import subprocess
    subprocess.check_call([sys.executable, "-m", "pip", "install", "--quiet", "numpy"])
    import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
LIB = os.path.join(HERE, "..", "build", "libtinypro5_test.so")

lib = ctypes.CDLL(os.path.abspath(LIB))
lib.tp5h_param_key.restype = ctypes.c_char_p
lib.tp5h_preset_name.restype = ctypes.c_char_p
for fn in ("tp5h_get", "tp5h_param_min", "tp5h_param_max", "tp5h_param_def", "tp5h_peak_pre", "tp5h_peak_post", "tp5h_ceiling"):
    getattr(lib, fn).restype = ctypes.c_float
lib.tp5h_init.argtypes = [ctypes.c_float]
lib.tp5h_set.argtypes = [ctypes.c_int, ctypes.c_float]
lib.tp5h_param_id.argtypes = [ctypes.c_char_p]
lib.tp5h_nan_resets.restype = ctypes.c_uint
lib.tp5h_render.argtypes = [ctypes.POINTER(ctypes.c_float), ctypes.POINTER(ctypes.c_float), ctypes.c_int]
lib.tp5h_scan.argtypes = [ctypes.c_char_p]
lib.tp5h_scan_report.restype = ctypes.c_char_p
lib.tp5h_bank_name.restype = ctypes.c_char_p
lib.tp5h_bank_name.argtypes = [ctypes.c_int]
lib.tp5h_bank_size.argtypes = [ctypes.c_int]
lib.tp5h_bank_program_name.restype = ctypes.c_char_p
lib.tp5h_bank_program_name.argtypes = [ctypes.c_int, ctypes.c_int]
lib.tp5h_load_bank_program.argtypes = [ctypes.c_int, ctypes.c_int]

SR = 44100.0
fails = []


def check(cond, msg):
    if not cond:
        fails.append(msg)
        print("  FAIL:", msg)


def render(seconds):
    n = int(seconds * SR)
    out = np.empty(n, dtype=np.float32)
    done = 0
    l = (ctypes.c_float * 128)()
    r = (ctypes.c_float * 128)()
    while done < n:
        k = min(128, n - done)
        lib.tp5h_render(l, r, k)
        out[done:done + k] = np.frombuffer(l, dtype=np.float32, count=k)
        done += k
    return out


def param_id(key):
    i = lib.tp5h_param_id(key.encode())
    check(i >= 0, "engine has no parameter '%s'" % key)
    return i


def chord(notes=(36, 43, 50, 57, 64), vel=110):
    for note in notes:
        lib.tp5h_note_on(note, vel)


def verify_banks(folder):
    """Scans `folder` with the module's loader and renders every program of every custom bank. Returns the number of failures."""
    before = len(fails)
    n = lib.tp5h_scan(os.fsencode(folder))
    print("scan:", lib.tp5h_scan_report().decode(), "(%d custom banks)" % n)
    ceiling = lib.tp5h_ceiling()
    rev_id, vint_id, vamp_id, vfilt_id = param_id("rev"), param_id("vintage"), param_id("vel_amp"), param_id("vel_filter")
    for b in range(1, lib.tp5h_bank_count()):
        size = lib.tp5h_bank_size(b)
        bank = lib.tp5h_bank_name(b).decode()
        rev4 = vint = vel = silent = 0
        for slot in range(size):
            lib.tp5h_init(ctypes.c_float(SR))
            lib.tp5h_load_bank_program(b, slot)
            name = "%s / %s" % (bank, lib.tp5h_bank_program_name(b, slot).decode())
            r = lib.tp5h_get(rev_id)
            check(r in (0.0, 1.0), "%s: rev %g" % (name, r))
            rev4 += r == 1.0
            vint += lib.tp5h_get(vint_id) > 0.0
            vel += lib.tp5h_get(vamp_id) > 0.0 or lib.tp5h_get(vfilt_id) > 0.0
            lib.tp5h_set(param_id("master_vol"), 1.0)
            lib.tp5h_reset_diag()
            chord((48, 55, 60))
            audio = render(3.0)
            check(bool(np.isfinite(audio).all()), "%s: NaN/Inf in output" % name)
            check(lib.tp5h_nan_resets() == 0, "%s: filter safety net fired" % name)
            check(lib.tp5h_peak_post() <= ceiling + 1e-6, "%s: output above the ceiling" % name)
            silent += float(np.abs(audio).max()) < 0.0005
        print("  %-34s %4d programs  %4d Rev 4  %4d vintage>0  %4d velocity depth  %3d silent" % (bank[:34], size, rev4, vint, vel, silent))
    return len(fails) - before


def main():
    if "--banks" in sys.argv:
        folder = sys.argv[sys.argv.index("--banks") + 1]
        return 1 if verify_banks(folder) else 0
    count = lib.tp5h_preset_count()
    pcount = lib.tp5h_param_count()
    print("engine: %d parameters, %d presets" % (pcount, count))
    check(count == 32, "preset count %d != 32" % count)
    names = [lib.tp5h_preset_name(i).decode() for i in range(count)]
    check(len(set(names)) == count, "preset names must be unique")
    check(names[0] == "Night Drive Bass" and names[8] == "Glass Clap" and names[16] == "Dark Brass Rise" and names[24] == "Falling Siren",
          "the four groups of eight start at slots 0, 8, 16 and 24")

    worst = (0.0, "")
    for i in range(count):
        lib.tp5h_init(ctypes.c_float(SR))
        lib.tp5h_load_preset(i)
        listed = {}
        for k in range(lib.tp5h_preset_kv_count(i)):
            pid, val = ctypes.c_int(), ctypes.c_float()
            lib.tp5h_preset_kv(i, k, ctypes.byref(pid), ctypes.byref(val))
            listed[pid.value] = val.value
        for pid in range(pcount):
            if not lib.tp5h_is_sound_param(pid):
                continue
            got = lib.tp5h_get(pid)
            lo, hi = lib.tp5h_param_min(pid), lib.tp5h_param_max(pid)
            if pid in listed:
                want = min(hi, max(lo, listed[pid]))
            else:
                want = lib.tp5h_param_def(pid)
            check(abs(got - want) < 1e-6, "%s: %s = %g, expected %g" % (names[i], lib.tp5h_param_key(pid).decode(), got, want))
        lib.tp5h_set(lib.tp5h_param_id(b"master_vol"), 1.0)
        lib.tp5h_reset_diag()
        for note in (36, 43, 50, 57, 64):
            lib.tp5h_note_on(note, 127)
        audio = render(8.0)  # the slowest swells (attack knob .78, ~2.7 s) are fully open by then
        check(bool(np.isfinite(audio).all()), "%s: NaN/Inf in output" % names[i])
        check(float(np.abs(audio).max()) > 0.004, "%s: silent" % names[i])
        pre = lib.tp5h_peak_pre()
        check(pre <= lib.tp5h_ceiling(), "%s: pre-limiter peak %.2f dBFS above the ceiling" % (names[i], 20 * math.log10(pre)))
        check(lib.tp5h_nan_resets() == 0, "%s: filter safety net fired" % names[i])
        if pre > worst[0]:
            worst = (pre, names[i])
    print("loudest preset: %s at %.2f dBFS (ceiling %.2f)" % (worst[1], 20 * math.log10(worst[0]), 20 * math.log10(lib.tp5h_ceiling())))

    # the new parameters: factory programs list none of them; every program also plays in Rev 4 voicing
    rev_id, vint_id = param_id("rev"), param_id("vintage")
    for key in ("rev", "vintage", "vel_filter", "vel_amp"):
        check(lib.tp5h_is_sound_param(param_id(key)) == 1, "%s must be a sound parameter" % key)
    loudest4 = (0.0, "")
    for i in range(count):
        lib.tp5h_init(ctypes.c_float(SR))
        lib.tp5h_load_preset(i)
        check(lib.tp5h_get(rev_id) == 0.0 and lib.tp5h_get(vint_id) == 0.0, "%s: factory program must be Rev 2 / vintage 0" % names[i])
        lib.tp5h_set(rev_id, 1.0)
        check(lib.tp5h_get(rev_id) == 1.0, "rev did not take")
        lib.tp5h_set(lib.tp5h_param_id(b"master_vol"), 1.0)
        lib.tp5h_reset_diag()
        chord((36, 43, 50, 57, 64), 127)
        audio = render(4.0)
        check(bool(np.isfinite(audio).all()), "%s (Rev 4): NaN/Inf in output" % names[i])
        check(lib.tp5h_nan_resets() == 0, "%s (Rev 4): filter safety net fired" % names[i])
        check(lib.tp5h_peak_post() <= lib.tp5h_ceiling() + 1e-6, "%s (Rev 4): output above the ceiling" % names[i])
        if lib.tp5h_peak_pre() > loudest4[0]:
            loudest4 = (lib.tp5h_peak_pre(), names[i])
    print("Rev 4 voicing of the same programs: loudest %s at %.2f dBFS before the limiter" % (loudest4[1], 20 * math.log10(loudest4[0])))
    print("RESULT:", "FAIL (%d)" % len(fails) if fails else "PASS - %d presets verified in memory" % count)
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
