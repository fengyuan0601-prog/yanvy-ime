#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
"""Verify and stage the bundled offline schemas into an isolated profile."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil

ROOT = Path(__file__).resolve().parents[1]
FILES = {
    "rime-wubi": ["wubi86.schema.yaml", "wubi86.dict.yaml"],
    "rime-pinyin-simp": ["pinyin_simp.schema.yaml", "pinyin_simp.dict.yaml"],
    "rime-stroke": ["stroke.schema.yaml", "stroke.dict.yaml"],
    "rime-prelude": ["default.yaml", "symbols.yaml", "punctuation.yaml", "key_bindings.yaml"],
}

def stage(shared: Path, user: Path):
    shared, user = shared.resolve(), user.resolve()
    if shared == user:
        raise ValueError("Shared dictionaries and writable user data must be separate.")
    for directory in (shared, user):
        if directory.exists() and any(directory.iterdir()):
            raise ValueError("Use new empty staging directories; existing profiles are never overwritten.")
    lock = json.loads((ROOT / "third_party/manifest.lock.json").read_text())
    records = {item["name"]: item for item in lock["sources"]}
    for repo, filenames in FILES.items():
        for filename in filenames:
            source = ROOT / "third_party" / repo / filename
            expected = records[repo]["files"][filename]
            if hashlib.sha256(source.read_bytes()).hexdigest() != expected:
                raise ValueError(f"Upstream file integrity check failed: {repo}/{filename}")
    shared.mkdir(parents=True, exist_ok=True)
    user.mkdir(parents=True, exist_ok=True)
    for repo, filenames in FILES.items():
        for filename in filenames:
            shutil.copy2(ROOT / "third_party" / repo / filename, shared / filename)
    for patch in sorted((ROOT / "schemas").glob("*.custom.yaml")):
        shutil.copy2(patch, user / patch.name)

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--shared-dir", type=Path, required=True)
    parser.add_argument("--user-dir", type=Path, required=True)
    args = parser.parse_args()
    stage(args.shared_dir, args.user_dir)
    print("Staged verified offline dictionaries and no-learning patches.")

if __name__ == "__main__":
    main()
