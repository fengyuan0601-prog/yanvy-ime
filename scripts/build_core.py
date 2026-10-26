#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
"""Build the real librime adapter; does not fetch libraries or install an IME."""
import argparse
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--rime-root", type=Path, help="Extracted Ubuntu .deb root, or Rime SDK root")
    p.add_argument("--output", type=Path, default=ROOT / "core/build/ime-core")
    args = p.parse_args()
    subprocess.run([sys.executable, str(ROOT / "scripts/generate_policy.py")], check=True)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    if sys.platform == "win32":
        raise SystemExit("Use core/CMakeLists.txt with Visual Studio and the Rime SDK on Windows; Windows has not been validated in this batch.")
    compiler = shutil.which("g++") or shutil.which("clang++")
    if not compiler:
        raise SystemExit("A C++17 compiler is required.")
    cmd = [compiler, "-std=c++17", "-O2", "-DNDEBUG", "-Wall", "-Wextra", "-Werror"]
    if args.rime_root:
        base = args.rime_root.resolve()
        include = base / "usr/include"
        if not (include / "rime_api.h").is_file():
            include = base / "include"
        candidates = [base / "usr/lib/x86_64-linux-gnu", base / "usr/lib/aarch64-linux-gnu", base / "lib"]
        lib = next((x for x in candidates if (x / "librime.so").exists()), None)
        if lib is None:
            raise SystemExit("librime.so is missing; provide the development package, not only the runtime.")
        cmd += ["-I", str(include), "-L", str(lib), f"-Wl,-rpath-link,{lib}"]
    cmd += [str(ROOT / "core/ime_core.cpp"), str(ROOT / "core/main.cpp"), "-lrime", "-o", str(args.output)]
    subprocess.run(cmd, check=True)
    print(args.output)
    if args.rime_root:
        print("For this extracted SDK, set LD_LIBRARY_PATH to " + str(lib) + " when running.")

if __name__ == "__main__":
    main()
