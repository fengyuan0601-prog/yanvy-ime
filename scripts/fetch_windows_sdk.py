#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
"""Explicit build-time download of the pinned x64 Rime SDK; never used at runtime."""
import argparse
import hashlib
from pathlib import Path
import subprocess
import urllib.request

URL = "https://github.com/rime/librime/releases/download/1.10.0/rime-295cb2a-Windows-clang.7z"
SHA256 = "5ceffc5f93312829c66adfd10eda5160b1802af955aa66ae2bdfd88286d9d363"
DLL_SHA256 = "6f86dd496524684f195bb89f3fb13fdf61a6b8e551c15353fac4e5955c945c1a"

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument("--cache-dir", type=Path, required=True)
    p.add_argument("--sdk-dir", type=Path, required=True)
    a=p.parse_args()
    a.cache_dir.mkdir(parents=True, exist_ok=True)
    archive=a.cache_dir/URL.rsplit("/",1)[1]
    if not archive.exists():
        pending=archive.with_suffix(".part")
        urllib.request.urlretrieve(URL,pending)
        pending.replace(archive)
    if hashlib.sha256(archive.read_bytes()).hexdigest()!=SHA256:
        raise SystemExit("SDK archive integrity check failed; the download was not executed.")
    if a.sdk_dir.exists() and any(a.sdk_dir.iterdir()):
        dll=a.sdk_dir/"dist/lib/rime.dll"
        if not dll.is_file() or hashlib.sha256(dll.read_bytes()).hexdigest()!=DLL_SHA256:
            raise SystemExit("Use a new SDK directory; existing files are not overwritten.")
    else:
        a.sdk_dir.mkdir(parents=True,exist_ok=True)
        subprocess.run(["tar","-xf",str(archive),"-C",str(a.sdk_dir)],check=True)
    assert hashlib.sha256((a.sdk_dir/"dist/lib/rime.dll").read_bytes()).hexdigest()==DLL_SHA256
    print(a.sdk_dir.resolve())

if __name__=="__main__":main()
