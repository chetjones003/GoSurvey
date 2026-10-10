#!/usr/bin/env python3
"""Fix VS CMake path quoting in build.ninja for Ninja POST_BUILD on Windows.

CMake can generate COMMAND lines with caret-escaped quotes around paths that
contain spaces (e.g. "C:\\Program Files\\..."):
  cmd.exe /C "cd /D ... && ^"C:\\Program Files\\...\\cmake.exe^" -E ..."

Ninja invokes this COMMAND line directly (single cmd.exe layer, not nested),
so the caret escaping is wrong here: it makes cmd.exe treat the caret-quote
as a literal character instead of closing the quoted path, which breaks with
"'"C:\\Program' is not recognized...". The correct form for a single cmd.exe
layer is plain doubled quotes:
  cmd.exe /C "cd /D ... && "C:\\Program Files\\...\\cmake.exe" -E ..."

This strips any caret-escaping CMake added back down to plain quotes.
"""
import pathlib
import re
import sys

def fix_file(path: pathlib.Path) -> bool:
    text = path.read_bytes().decode("utf-8", errors="replace")
    orig = text
    # ^"C:\Program Files -> "C:\Program Files
    text = re.sub(r'\^"(C:\\Program Files)', r'"\1', text)
    # cmake.exe^" / ctest.exe^" -> cmake.exe" / ctest.exe"
    text = re.sub(r'(cmake\.exe|ctest\.exe)\^"', r'\1"', text)
    if text != orig:
        path.write_bytes(text.encode("utf-8"))
        return True
    return False

if __name__ == "__main__":
    for arg in sys.argv[1:]:
        p = pathlib.Path(arg)
        if p.is_file():
            changed = fix_file(p)
            print(f"{p}: {'fixed' if changed else 'no change'}")
        else:
            print(f"{p}: not found", file=sys.stderr)
