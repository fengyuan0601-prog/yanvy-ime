#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
"""Run the actual NSIS flow with synthetic callbacks and an isolated HKCU key.

This is not a system install/uninstall or TSF compatibility test. Every file
stays in a new fixture directory; no production registration or broker call.
"""
import argparse
import ctypes
import os
from pathlib import Path
import shutil
import subprocess
import time
import uuid
import winreg

ROOT=Path(__file__).resolve().parents[1]
PRODUCT_KEY=r'Software\Microsoft\Windows\CurrentVersion\Uninstall\YANVY-dev'

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--fixture-binary',type=Path,required=True)
    parser.add_argument('--makensis',type=Path,required=True)
    parser.add_argument('--temp-dir',type=Path,required=True)
    parser.add_argument('--scenario',action='append',help='Run only the named scenario; may be repeated.')
    a=parser.parse_args()
    base=a.temp_dir.resolve()/('yanvy-transaction-'+uuid.uuid4().hex)
    assert base.is_relative_to(a.temp_dir.resolve()) and not base.exists()
    base.mkdir(parents=True)
    ctypes.windll.kernel32.SetErrorMode(0x0001 | 0x0002)
    scenarios=('x86-check-failure','check-failure','check-crash','registration-failure','rollback-failure',
               'rollback-unregister-failure','legacy-registration-failure','legacy-rollback-failure',
               'success','unicode-success','unsafe-previous','uninstall-failure','wrong-runtime-target')
    if a.scenario and not set(a.scenario).issubset(scenarios):parser.error('Unknown scenario')
    for scenario in scenarios:
        if a.scenario and scenario not in a.scenario:continue
        root=base/('选择位置 测试' if scenario=='unicode-success' else 'standard')/scenario
        stage=root/'stage';old=root/'old';new=root/'new';stage.mkdir(parents=True);old.mkdir()
        # The real elevated bootstrap has already created this empty protected
        # version directory before invoking the NSIS payload.
        if scenario!='wrong-runtime-target':new.mkdir()
        key_path='Software\\YANVY-Installer-Fixture\\'+uuid.uuid4().hex
        def read_key(name):
            with winreg.OpenKey(winreg.HKEY_CURRENT_USER,key_path,0,winreg.KEY_READ|winreg.KEY_WOW64_64KEY) as key:return winreg.QueryValueEx(key,name)[0]
        for folder in (stage,old):
            for name in ('YANVY-Server.exe','YANVY-Setup.exe','YANVY-Settings.exe','YANVY-Check-x86.exe','Uninstall.exe'):shutil.copy2(a.fixture_binary,folder/name)
            (folder/'YANVY-INSTALL-MARKER.txt').write_text('YANVY fixture\n')
        # An old writable directory may be tampered with. Every path through
        # setup must use the current helper, even during rollback.
        for name in ('YANVY-Setup.exe','Uninstall.exe'):
            (old/name).write_bytes(b'not an executable: never run old code')
        (stage/'TESTING.txt').write_text('Synthetic NSIS transaction fixture only.\n')
        (old/'settings.ini').write_text('[display]\nfont_size=23\ntheme=2\nunknown=preserve-only-in-old\n',encoding='ascii')
        (old/'keep.txt').write_text('unrelated old file must survive')
        if scenario=='unsafe-previous':(old/'YANVY-INSTALL-MARKER.txt').unlink()
        if scenario=='check-failure':(stage/'check-result.txt').write_text('1')
        if scenario=='x86-check-failure':(stage/'x86-check-result.txt').write_text('1')
        if scenario=='check-crash':(stage/'check-result.txt').write_text('-1073741795')  # 0xC000001D, simulated exception status
        if scenario in ('registration-failure','rollback-failure','rollback-unregister-failure',
                        'legacy-registration-failure','legacy-rollback-failure'):
            (stage/'register-result.txt').write_text('1')
        if scenario=='rollback-failure':(stage/'restore-result.txt').write_text('1')
        if scenario=='rollback-unregister-failure':(stage/'unregister-result.txt').write_text('1')
        if scenario.startswith('legacy-'):(stage/'legacy-profile-result.txt').write_text('0')
        if scenario=='legacy-rollback-failure':(stage/'unregister-result.txt').write_text('1')
        (root/'stage-version.nsh').write_text('!define YANVY_VERSION "0.1.0-rc.1"\n!define YANVY_WINDOWS_VERSION "0.1.0.3"\n')
        (root/'stage-uninstall.nsh').write_text('\n'.join('Delete /REBOOTOK "$INSTDIR\\'+p.name+'"' for p in stage.iterdir())+'\n')
        source=(ROOT/'platforms/windows/installer/yanvy.nsi').read_text(encoding='utf-8-sig')
        assert PRODUCT_KEY in source
        source=source.replace(PRODUCT_KEY,key_path).replace(' HKLM ',' HKCU ')
        source=source.replace('$SMPROGRAMS',str(root/'shortcuts'))
        runtime='$PROGRAMFILES64\\YANVY\\${YANVY_VERSION}'
        assert source.count(runtime)==2
        source=source.replace(runtime,str(new))
        icon=str(ROOT/'assets/brand/yanvy.ico')
        source=source.replace('..\\..\\..\\assets\\brand\\yanvy.ico',icon)
        script=root/'transaction.nsi';script.write_text(source,encoding='utf-8-sig')
        assert PRODUCT_KEY not in source and ' HKLM ' not in source and '$SMPROGRAMS' not in source and '$PROGRAMFILES64' not in source
        installer=root/'transaction.exe'
        subprocess.run([str(a.makensis),'/V2',f'/DSTAGE={stage}',f'/DOUTPUT={installer}',str(script)],check=True,timeout=30)
        if scenario!='rollback-unregister-failure':
            with winreg.CreateKeyEx(winreg.HKEY_CURRENT_USER,key_path,0,winreg.KEY_WRITE|winreg.KEY_WOW64_64KEY) as key:
                winreg.SetValueEx(key,'InstallLocation',0,winreg.REG_SZ,str(old));winreg.SetValueEx(key,'DisplayVersion',0,winreg.REG_SZ,'fixture-old')
        env=dict(os.environ,YANVY_SETUP_TEMP_READY='1',YANVY_PREVIOUS_CAN_CLEAN='1',TEMP=str(a.temp_dir.resolve()),TMP=str(a.temp_dir.resolve()))
        # Match the native launcher: NSIS requires /D= last and unquoted,
        # including when the selected directory contains spaces.
        install_command=subprocess.list2cmdline([str(installer)])+' /S /D='+str(root/'forbidden target' if scenario=='wrong-runtime-target' else new)
        result=subprocess.run(install_command,env=env,timeout=90)
        expected=23 if scenario=='x86-check-failure' else 0xA1260001 if scenario=='check-failure' else 0xC000001D if scenario=='check-crash' else 30 if scenario in ('registration-failure','legacy-registration-failure') else 31 if scenario.startswith('rollback-') or scenario=='legacy-rollback-failure' else 33 if scenario=='unsafe-previous' else 41 if scenario=='wrong-runtime-target' else 0
        assert result.returncode==expected,(scenario,result.returncode)
        old_trace=(old/'fixture-trace.txt').read_text() if (old/'fixture-trace.txt').exists() else ''
        assert not old_trace,(scenario,'old executable was invoked')
        if scenario=='wrong-runtime-target':
            assert not new.exists() and not (root/'forbidden target').exists()
            assert read_key('InstallLocation')==str(old)
        elif scenario in ('x86-check-failure','check-failure','check-crash'):
            assert not old_trace and read_key('InstallLocation')==str(old)
            assert (old/'YANVY-Setup.exe').exists() and (old/'Uninstall.exe').exists()
            assert not (new/'YANVY-Setup.exe').exists()
        elif scenario in ('registration-failure','legacy-registration-failure'):
            assert read_key('InstallLocation')==str(old)
            assert (old/'YANVY-Setup.exe').exists() and (old/'Uninstall.exe').exists()
            assert read_key('DisplayVersion')=='fixture-old'
            assert (new/'fixture-trace.txt').read_text()=='check\nregister\nunregister\n'+('restore\n' if scenario=='registration-failure' else '')
        elif scenario.startswith('rollback-') or scenario=='legacy-rollback-failure':
            assert (new/'YANVY-Setup.exe').exists() and (new/'Uninstall-core.exe').exists()
            assert (new/'fixture-trace.txt').read_text()=='check\nregister\nunregister\n'+('restore\n' if scenario=='rollback-failure' else '')
            if scenario=='rollback-failure':
                assert read_key('InstallLocation')==str(old)
        elif scenario=='unsafe-previous':
            assert read_key('InstallLocation')==str(new) and (old/'YANVY-Setup.exe').exists()
            assert (new/'fixture-trace.txt').read_text()=='check\nregister\nretire\n'
        else:
            assert not old_trace and read_key('InstallLocation')==str(new)
            assert (new/'fixture-trace.txt').read_text()=='check\nregister\nretire\n'
            assert not (old/'YANVY-Setup.exe').exists() and not (old/'Uninstall.exe').exists()
            assert (old/'keep.txt').read_text()=='unrelated old file must survive'
            # Preferences migrate in the normal-user UI, never in this admin
            # payload. An arbitrary settings file remains user-owned.
            assert not (new/'settings.ini').exists()
            (new/'settings.ini').write_text('[display]\nfont_size=19\n',encoding='ascii')
            (new/'keep.txt').write_text('must survive exact-file removal')
            if scenario=='uninstall-failure':
                (new/'unregister-result.txt').write_text('1')
                command=subprocess.list2cmdline([str(new/'Uninstall-core.exe')])+' /S _?='+str(new)
                failed=subprocess.run(command,env=env,timeout=30)
                assert failed.returncode==30
                assert (new/'YANVY-Setup.exe').exists() and read_key('InstallLocation')==str(new)
                assert (new/'fixture-trace.txt').read_text()=='check\nregister\nretire\nunregister\n'
                (new/'unregister-result.txt').unlink()
            command=subprocess.list2cmdline([str(new/'Uninstall-core.exe')])+' /S _?='+str(new)
            removed=subprocess.run(command,env=env,timeout=30)
            assert removed.returncode in (0,3010)
            for _ in range(50):
                if not (new/'YANVY-Setup.exe').exists():break
                time.sleep(.1)
            assert (new/'keep.txt').read_text()=='must survive exact-file removal'
            assert (new/'settings.ini').exists() and not (new/'YANVY-Setup.exe').exists()
            assert (new/'fixture-trace.txt').read_text()=='check\nregister\nretire\n'+('unregister\n' if scenario=='uninstall-failure' else '')+'unregister\nstop\n'
            try:read_key('InstallLocation')
            except FileNotFoundError:pass
            else:raise AssertionError('fixture uninstall key was not removed')
        assert (old/'settings.ini').read_text().endswith('unknown=preserve-only-in-old\n')
        try:winreg.DeleteKeyEx(winreg.HKEY_CURRENT_USER,key_path,winreg.KEY_WOW64_64KEY)
        except FileNotFoundError:pass
        print(scenario+': PASS (isolated callback/registry fixture)')
    print('Fixture evidence retained at '+str(base))

if __name__=='__main__':main()
