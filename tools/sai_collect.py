#!/usr/bin/env python3
"""sai_collect.py -- concatenate makeqstrdefs output files for CMake.

Simple stand-in for `cat a b c > out` on Windows.

Usage: sai_collect.py <output> <input> [<input> ...]
"""
import sys

def main():
    if len(sys.argv) < 3:
        sys.exit("usage: sai_collect.py <output> <input>...")
    out, inputs = sys.argv[1], sys.argv[2:]
    with open(out, "w", encoding="utf-8") as f:
        for path in inputs:
            try:
                with open(path, "r", encoding="utf-8") as g:
                    f.write(g.read())
            except FileNotFoundError:
                pass

if __name__ == "__main__":
    main()
