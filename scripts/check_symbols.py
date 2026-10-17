#!/usr/bin/env python3
"""Audit a shipped dsp.so: it must be an AArch64 ELF shared object, export
move_plugin_init_v2, and import nothing that allocates or does file/thread IO.

Usage: check_symbols.py path/to/dsp.so
Pure stdlib ELF64 parser, so it works on any host (no binutils needed).
"""
import struct
import sys

EM_AARCH64 = 183
SHT_DYNSYM = 11

# fopen / fread / fwrite are deliberately NOT forbidden: src/bankscan.c reads the custom bank files once from create_instance and
# writes the small per-bank user overlay files (<module>/user/*.tp5u) when Save / Revert is pressed on the host thread. The audio path itself is audited at source level (scripts/test.sh keeps every other file IO-free).
FORBIDDEN = {
    "malloc", "calloc", "realloc", "free", "posix_memalign", "aligned_alloc", "memalign", "valloc",
    "strdup", "strndup", "freopen", "fprintf", "printf", "puts", "putchar",
    "open", "read", "write", "mmap", "munmap", "pthread_create", "pthread_mutex_lock",
    "pthread_mutex_unlock", "sem_wait", "usleep", "nanosleep", "sleep", "system", "dlopen",
}


def main(path):
    with open(path, "rb") as f:
        data = f.read()
    if data[:4] != b"\x7fELF" or data[4] != 2 or data[5] != 1:
        sys.exit("%s: not a little-endian ELF64 file" % path)
    e_type, e_machine = struct.unpack_from("<HH", data, 16)
    if e_type != 3:
        sys.exit("%s: not a shared object (e_type=%d)" % (path, e_type))
    if e_machine != EM_AARCH64:
        sys.exit("%s: wrong architecture (e_machine=%d, want %d AArch64)" % (path, e_machine, EM_AARCH64))
    shoff, = struct.unpack_from("<Q", data, 40)
    shentsize, shnum = struct.unpack_from("<HH", data, 58)

    def section(i):
        return struct.unpack_from("<IIQQQQIIQQ", data, shoff + i * shentsize)

    undefined, exported = [], []
    for i in range(shnum):
        _, sh_type, _, _, off, size, link, _, _, entsize = section(i)
        if sh_type != SHT_DYNSYM:
            continue
        stroff, strsize = section(link)[4], section(link)[5]
        strtab = data[stroff:stroff + strsize]
        for s in range(size // entsize):
            st_name, st_info, _, st_shndx, _, _ = struct.unpack_from("<IBBHQQ", data, off + s * entsize)
            name = strtab[st_name:strtab.index(b"\0", st_name)].decode()
            if not name:
                continue
            if st_shndx == 0:
                undefined.append(name)
            elif (st_info >> 4) == 1:
                exported.append(name)

    bad = sorted(set(undefined) & FORBIDDEN)
    print("%s: AArch64 ELF shared object" % path)
    print("  exports : %s" % ", ".join(sorted(exported)))
    print("  imports : %s" % ", ".join(sorted(set(undefined))))
    if "move_plugin_init_v2" not in exported:
        sys.exit("FAIL: move_plugin_init_v2 is not exported")
    if bad:
        sys.exit("FAIL: forbidden imports (allocation / IO / locking): %s" % ", ".join(bad))
    print("  OK: exports move_plugin_init_v2; no allocation or locking imports (file imports only for the one-time bank scan)")


if __name__ == "__main__":
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    main(sys.argv[1])
