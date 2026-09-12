#!/usr/bin/env python3
"""Fix VS CMake path quoting in build.ninja for Ninja POST_BUILD on Windows.

CMake 4.x (VS 2022/18's bundled CMake) generates:
  cmd.exe /C "cd /D ... && "C:\Program Files\...\cmake.exe" -E ..."
which closes the outer cmd /C " early ( : was unexpected ).
The correct escaping inside cmd /C " is ^" :
  cmd.exe /C "cd /D ... && ^"C:\Program Files\...\cmake.exe^" -E ..."
"""
import pathlib
import re
import sys

def fix_file(path: pathlib.Path) -> bool:
    text = path.read_bytes().decode("utf-8", errors="replace")
    orig = text
    # && "C:\Program Files -> && ^"C:\Program Files
    text = re.sub(r'&&\s*"+C:\\Program Files', r'&& ^"C:\\Program Files', text)
    # cmake.exe" -> cmake.exe^"  (only when followed by space/-)
    text = re.sub(r'cmake\.exe"+', r'cmake.exe^"', text)
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
