#!/usr/bin/env python3
"""sai_qstr_wrap.py -- qstrdefs merge for makeqstrdata, no preprocessor needed.

The upstream pipeline is `cat qstrdefs.h collected | sed wrap | cpp | sed
unwrap`.  Only a handful of #if conditionals appear in qstrdefs.h, and their
macros are known constants of this port, so we evaluate them directly in
Python.  This works identically on MSVC/clang/gcc and needs no shell.

Usage:
  sai_qstr_wrap.py --qstrdefs <files...> --output <file> [--define N=V ...]
"""
import sys


# Conditionals that actually occur in py/qstrdefs.h, evaluated for the
# sai.L99 MicroPython configuration (see ports/micropython/mpconfigport.h).
TRUE_MACROS = {
    "MICROPY_PY_BUILTINS_STR_OP_MODULO": "0",   # core-features ROM level disables it? no:
}

# We instead mirror the actual config:
CONFIG = {
    "MICROPY_PY_SYS_PS1_PS2": "1",
    "MICROPY_PY_BUILTINS_STR_OP_MODULO": "1",
    "MICROPY_MODULE_FROZEN": "0",
    "MICROPY_VFS_ROM": "0",
    "MICROPY_VFS_ROM_IOCTL": "0",
    "MICROPY_ENABLE_PYSTACK": "0",
}


def truth(tok):
    return CONFIG.get(tok, "0") not in ("0", "")


def main():
    argv = sys.argv[1:]
    files = []
    output = None
    mode = None
    for a in argv:
        if a == "--qstrdefs":
            mode = "f"
        elif a in ("--output", "-o"):
            mode = "o"
        elif mode == "f":
            files.append(a)
        elif mode == "o":
            output = a
    if not files or not output:
        sys.exit("usage: sai_qstr_wrap.py --qstrdefs <files...> --output <file>")

    out_lines = []
    stack = []          # (parent_active, this_branch_taken_ever, this_branch_active)
    for path in files:
        with open(path, "r", encoding="utf-8", errors="replace") as f:
            for raw in f:
                s = raw.rstrip("\r\n")
                t = s.strip()
                if t.startswith("#if"):
                    parent = all(branch for _, branch, _ in stack) if stack else True
                    cond = t[3:].strip()
                    val = truth(cond) if parent else False
                    stack.append((parent, val, val))
                    continue
                if t.startswith("#ifdef"):
                    parent = all(branch for _, branch, _ in stack) if stack else True
                    val = truth(t[6:].strip()) if parent else False
                    stack.append((parent, val, val))
                    continue
                if t.startswith("#ifndef"):
                    parent = all(branch for _, branch, _ in stack) if stack else True
                    val = (not truth(t[7:].strip())) if parent else False
                    stack.append((parent, val, val))
                    continue
                if t.startswith("#elif"):
                    if not stack:
                        continue
                    parent, taken, _ = stack[-1]
                    cond = t[5:].strip()
                    val = (parent and not taken and truth(cond))
                    stack[-1] = (parent, taken or val, val)
                    continue
                if t.startswith("#else"):
                    if not stack:
                        continue
                    parent, taken, _ = stack[-1]
                    val = parent and not taken
                    stack[-1] = (parent, True, val)
                    continue
                if t.startswith("#endif"):
                    if stack:
                        stack.pop()
                    continue
                active = all(branch for _, branch, _ in stack) if stack else True
                if not active:
                    continue
                if t.startswith("Q(") and t.endswith(")"):
                    out_lines.append(t)
    with open(output, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(out_lines) + "\n")


if __name__ == "__main__":
    main()
