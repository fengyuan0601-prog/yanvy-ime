#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
"""Isolated pipe ownership and handoff test; never touches installed pipes."""
import argparse
import ctypes as c
from ctypes import wintypes as w
import os
from pathlib import Path
import re
import subprocess
import sys
import time
import uuid

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--root',type=Path,required=True)
    p.add_argument('--temp-dir',type=Path,required=True)
    p.add_argument('--client-x86',type=Path,help='Optional 32-bit synthetic IPC client')
    a=p.parse_args()
    namespace=uuid.uuid4().hex
    identity=subprocess.check_output(['whoami','/user','/fo','csv','/nh'],text=True)
    sid=re.search(r'S-1-(?:\d+-)+\d+',identity)
    if not sid:raise RuntimeError('Cannot obtain current user SID')
    k=c.WinDLL('kernel32',use_last_error=True)
    k.ProcessIdToSessionId.argtypes=[w.DWORD,c.POINTER(w.DWORD)]
    session=w.DWORD();assert k.ProcessIdToSessionId(os.getpid(),c.byref(session))
    k.CreateNamedPipeW.argtypes=[w.LPCWSTR,w.DWORD,w.DWORD,w.DWORD,w.DWORD,w.DWORD,w.DWORD,c.c_void_p]
    k.CreateNamedPipeW.restype=w.HANDLE
    k.CloseHandle.argtypes=[w.HANDLE]
    k.WaitNamedPipeW.argtypes=[w.LPCWSTR,w.DWORD]
    base=rf'\\.\pipe\YANVY-v{{}}-{sid.group()}-{session.value}-test-{namespace}'
    # Hold the v4 name before starting the fixture. The new broker must leave
    # this pipe alone until its owner closes it, then acquire it for forwarding.
    held=k.CreateNamedPipeW(base.format(4),0x00080003,0x0000000c,1,64,64,500,None)
    assert held!=w.HANDLE(-1).value,c.get_last_error()
    root=a.root.resolve();profile=a.temp_dir.resolve()/('yanvy-conflict-'+namespace)
    fixture=root/'YANVY-Broker-Fixture.exe'
    process=subprocess.Popen([str(fixture),'--serve-test',str(profile),namespace])
    try:
        deadline=time.monotonic()+30
        while not k.WaitNamedPipeW(base.format(7),100):
            if process.poll() is not None or time.monotonic()>deadline:raise RuntimeError('Fixture broker failed to start')
            time.sleep(.05)
        duplicate=k.CreateNamedPipeW(base.format(4),0x00080003,0x0000000c,1,64,64,500,None)
        assert duplicate==w.HANDLE(-1).value,'Existing legacy pipe was unexpectedly replaced'
        k.CloseHandle(held);held=None
        result=subprocess.run([sys.executable,str(Path(__file__).with_name('test_windows_broker.py')),
            '--root',str(root),'--profile-dir',str(profile),'--report',str(a.temp_dir/('yanvy-conflict-report-'+namespace+'.json')),
            '--namespace',namespace],timeout=90)
        assert result.returncode==0,result.returncode
        if a.client_x86:
            result=subprocess.run([str(a.client_x86.resolve()),namespace],timeout=30)
            assert result.returncode==0,result.returncode
        print('Existing legacy pipe preserved, then handed off: PASS')
    finally:
        if held:k.CloseHandle(held)
        subprocess.run([str(fixture),'--stop-test',namespace],timeout=10)
        process.wait(timeout=15)

if __name__=='__main__':main()
