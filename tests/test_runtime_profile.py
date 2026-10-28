#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
"""A pre-existing altered user patch must never be silently overwritten."""
import argparse
from pathlib import Path
import shutil
import subprocess

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root',type=Path,required=True)
    parser.add_argument('--profile',type=Path,required=True)
    args=parser.parse_args()
    root=args.root.resolve();profile=args.profile.resolve()
    if profile.exists():raise SystemExit('Use a fresh isolated profile directory.')
    shutil.copytree(root/'profile',profile)
    patch=profile/'default.custom.yaml'
    changed=patch.read_bytes()+b'\n# synthetic modification\n';patch.write_bytes(changed)
    result=subprocess.run([str(root/'YANVY-Broker-Fixture.exe'),'--serve-test',str(profile)],timeout=30)
    assert result.returncode==5,result.returncode
    assert patch.read_bytes()==changed
    assert not any('userdb' in item.name for item in profile.rglob('*'))
    print('Altered user profile rejected without replacement or user database: PASS')

if __name__=='__main__':main()
