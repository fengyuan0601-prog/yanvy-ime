#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
"""Check setup path boundaries using the actual native parser; no system changes."""
import argparse
from pathlib import Path
import subprocess

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary',type=Path,required=True)
    binary=parser.parse_args().binary.resolve()
    cases=[(r'E:\Codex\Programs\YANVY\0.1.0-rc.1',True),
           ('E:\\言舟 测试\\YANVY\\0.1.0-rc.1',True),
           ('E:\\',False),('C:\\Programs\\YANVY',False),
           ('D:\\YANVY',False),('..\\YANVY',False),
           ('\\\\server\\share\\YANVY',False),('\\\\?\\E:\\YANVY',False),
           ('E:\\safe\\..\\other',False),('E:\\safe\\.\\other',False),
           ('E:\\safe\\trailing.',False),('E:\\safe\\trailing ',False),
           ('E:\\safe\\name:stream',False),('E:\\safe\\"quoted"',False),
           ('E:\\safe\\line\nbreak',False)]
    for value,expected in cases:
        result=subprocess.run([str(binary),'--check-path',value],timeout=10)
        assert (result.returncode==0)==expected,repr(value)
    print(f'{len(cases)} installer path boundary checks PASS; no registration or installation performed')

if __name__=='__main__':main()
