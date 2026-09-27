#!/usr/bin/env python3
"""sai_run_tool.py -- run a python generator tool, capture stdout to a file.

CMake VERBATIM custom commands cannot use shell redirects, and the
MicroPython makefile-style generators print their result to stdout.

Usage: sai_run_tool.py --output <file> -- <script.py> [args...]
"""
import argparse
import subprocess
import sys


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--output", required=True)
    ap.add_argument("cmd", nargs=argparse.REMAINDER)
    args = ap.parse_args()

    cmd = args.cmd
    if cmd and cmd[0] == "--":
        cmd = cmd[1:]
    if not cmd:
        sys.exit("no command given")

    res = subprocess.run([sys.executable] + cmd, capture_output=True,
                         text=True, encoding="utf-8", errors="replace")
    if res.returncode != 0:
        sys.stderr.write(res.stderr)
        sys.exit("tool failed: " + " ".join(cmd))
    with open(args.output, "w", encoding="utf-8", newline="\n") as f:
        f.write(res.stdout)


if __name__ == "__main__":
    main()
