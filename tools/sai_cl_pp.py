#!/usr/bin/env python3
"""sai_cl_pp.py -- MSVC-friendly multi-file preprocessor for makeqstrdefs.

`cl -E` only writes preprocessed output to stdout for a SINGLE source file;
makeqstrdefs.py batches several files per invocation.  This wrapper runs cl
once per file (keeping #line markers via /E) and concatenates the output.

Usage: sai_cl_pp.py <cl.exe> [flags...] <file.c> [<file.c> ...]
Files are the arguments ending in .c; everything else is a flag.
"""
import subprocess
import sys


def main():
    argv = sys.argv[1:]
    if len(argv) < 2:
        sys.exit("usage: sai_cl_pp.py <cl.exe> [flags...] <file>...")
    cl = argv[0]
    files = [a for a in argv[1:] if a.lower().endswith(".c")]
    flags = [a for a in argv[1:] if a not in files]
    if not files:
        sys.exit("no source files given")

    rc = 0
    for f in files:
        res = subprocess.run([cl, "/nologo", "/E"] + flags + [f],
                             capture_output=True, text=True,
                             encoding="utf-8", errors="replace")
        if res.returncode != 0:
            sys.stderr.write(res.stderr)
            rc = res.returncode or 1
            continue
        sys.stdout.write(res.stdout)
    sys.exit(rc)


if __name__ == "__main__":
    main()
