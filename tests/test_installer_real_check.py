#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
"""Run real NSIS extraction and real librime check, with synthetic registration.

All installation paths, shortcuts and temporary files remain in a new fixture
under --temp-dir; the registry is an isolated HKCU key. Never registers TSF,
starts/stops a broker, or uses the installed product's data.
"""
import argparse
import ctypes
import os
from pathlib import Path
import shutil
import subprocess
import uuid
import winreg

ROOT = Path(__file__).resolve().parents[1]
PRODUCT_KEY = r'Software\Microsoft\Windows\CurrentVersion\Uninstall\YANVY-dev'

def main():
    p = argparse.ArgumentParser(description=__doc__)
    for name in ('stage', 'server-binary', 'fixture-binary', 'probe-binary', 'makensis', 'temp-dir'):
        p.add_argument('--'+name, type=Path, required=True)
    a = p.parse_args()
    base = a.temp_dir.resolve()/('yanvy-real-installer-'+uuid.uuid4().hex)
    assert base.is_relative_to(a.temp_dir.resolve()) and not base.exists()
    base.mkdir(parents=True)
    ctypes.windll.kernel32.SetErrorMode(0x0001 | 0x0002)
    for name, expected in (('real-engine',0),('real-upgrade',0),('modified-data',29),
                           ('missing-engine-dll',23),('registration-failure-retry',30)):
        root=base/('真实 引擎 '+name); stage=root/'stage'; target=root/'YANVY'/'0.1.0-rc.4'/'YANVY'/'test'
        shutil.copytree(a.stage,stage)
        shutil.copy2(a.server_binary,stage/'YANVY-Server.exe')
        shutil.copy2(a.probe_binary,stage/'real-dependency-probe.exe')
        shutil.copy2(a.fixture_binary,stage/'YANVY-Setup.exe')
        # Include this synthetic callback output in the fixture's removal
        # manifest; it does not exist in production packages.
        (stage/'fixture-trace.txt').write_text('')
        if name=='registration-failure-retry':(stage/'register-result.txt').write_text('1')
        if name=='modified-data':
            with (stage/'data/default.yaml').open('ab') as f: f.write(b'\n# intentionally changed\n')
        if name=='missing-engine-dll': (stage/'rime.dll').unlink()
        key_path='Software\\YANVY-Installer-Fixture\\'+uuid.uuid4().hex
        old=root/'old install'
        if name=='real-upgrade':
            old.mkdir();(old/'YANVY-Setup.exe').write_bytes(b'old executable changed; must never run')
            (old/'YANVY-INSTALL-MARKER.txt').write_text('YANVY fixture\n')
            (old/'settings.ini').write_text('[display]\nfont_size=23\n')
            (old/'keep.txt').write_text('preserve')
            with winreg.CreateKeyEx(winreg.HKEY_CURRENT_USER,key_path,0,winreg.KEY_WRITE|winreg.KEY_WOW64_64KEY) as key:
                winreg.SetValueEx(key,'InstallLocation',0,winreg.REG_SZ,str(old))
                winreg.SetValueEx(key,'DisplayVersion',0,winreg.REG_SZ,'fixture-old')
        (root/'stage-version.nsh').write_text('!define YANVY_VERSION "0.1.0-rc.4"\n!define YANVY_WINDOWS_VERSION "0.1.0.6"\n')
        lines=['Delete /REBOOTOK "$INSTDIR\\'+str(f.relative_to(stage)).replace('/','\\')+'"' for f in stage.rglob('*') if f.is_file()]
        directories=sorted((d for d in stage.rglob('*') if d.is_dir()),key=lambda d:len(d.parts),reverse=True)
        lines+=['RMDir "$INSTDIR\\'+str(d.relative_to(stage)).replace('/','\\')+'"' for d in directories]
        (root/'stage-uninstall.nsh').write_text('\n'.join(lines)+'\n')
        source=(ROOT/'platforms/windows/installer/yanvy.nsi').read_text(encoding='utf-8-sig')
        source=source.replace(PRODUCT_KEY,key_path).replace(' HKLM ',' HKCU ').replace('$SMPROGRAMS',str(root/'shortcuts'))
        runtime='$PROGRAMFILES64\\YANVY\\${YANVY_VERSION}'
        assert source.count(runtime)==2
        source=source.replace(runtime,str(target))
        source=source.replace('..\\..\\..\\assets\\brand\\yanvy.ico',str(ROOT/'assets/brand/yanvy.ico'))
        assert PRODUCT_KEY not in source and ' HKLM ' not in source and '$PROGRAMFILES64' not in source
        script=root/'test.nsi';script.write_text(source,encoding='utf-8-sig')
        installer=root/'test.exe'
        # The dual-architecture payload takes longer to compress on busy
        # machines. This bound applies only to the isolated test build.
        build_command=[str(a.makensis),'/V2',f'/DSTAGE={stage}',f'/DOUTPUT={installer}',str(script)]
        subprocess.run(build_command,check=True,timeout=180)
        target.mkdir(parents=True)  # Bootstrap prepares the empty protected runtime.
        env=dict(os.environ,YANVY_SETUP_TEMP_READY='1',YANVY_PREVIOUS_CAN_CLEAN='1',TEMP=str(a.temp_dir.resolve()),TMP=str(a.temp_dir.resolve()))
        try:
            result=subprocess.run(subprocess.list2cmdline([str(installer)])+' /S /D='+str(target),env=env,timeout=90)
            assert result.returncode==expected,(name,result.returncode,expected)
            if expected==0:
                with winreg.OpenKey(winreg.HKEY_CURRENT_USER,key_path,0,winreg.KEY_READ|winreg.KEY_WOW64_64KEY) as key:
                    assert winreg.QueryValueEx(key,'InstallLocation')[0]==str(target)
                assert (target/'profile/build/wubi86.schema.yaml').is_file()
                if name=='real-upgrade':
                    assert not (old/'YANVY-Setup.exe').exists()
                    assert (old/'keep.txt').read_text()=='preserve' and (old/'settings.ini').exists()
            else:
                assert not (target/'YANVY-Setup.exe').exists()
                assert not target.exists() or not any(target.iterdir()),(name,'rollback left files blocking retry')
                if name=='registration-failure-retry':
                    (stage/'register-result.txt').write_text('0')
                    subprocess.run(build_command,check=True,timeout=180)
                    target.mkdir(parents=True,exist_ok=True)
                    retry=subprocess.run(subprocess.list2cmdline([str(installer)])+' /S /D='+str(target),env=env,timeout=90)
                    assert retry.returncode==0,('same-path retry',retry.returncode)
                    assert (target/'profile/build/wubi86.schema.yaml').is_file()
            assert not (root/'must-not-use-default').exists()
            print(name+': PASS (real librime, synthetic registration)')
        finally:
            try:winreg.DeleteKeyEx(winreg.HKEY_CURRENT_USER,key_path,winreg.KEY_WOW64_64KEY)
            except FileNotFoundError:pass
    print('Fixture evidence retained at '+str(base))

if __name__=='__main__':main()
