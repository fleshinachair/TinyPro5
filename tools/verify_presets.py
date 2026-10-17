#!/usr/bin/env python3
"""In-memory preset verification through ctypes (no files written).

Loads build/libtinypro5_test.so (built by scripts/build_testlib.sh), then for every preset:
  * the compiled table entry loads and every listed parameter lands in the engine (clamped to range),
  * every parameter the preset does not list falls back to its default,
  * a five-note chord renders finite, audible audio and stays under the -6.1 dBFS ceiling.
Exit status is non-zero on any failure.
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


def main():
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
    print("RESULT:", "FAIL (%d)" % len(fails) if fails else "PASS - %d presets verified in memory" % count)
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
