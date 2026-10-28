#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
"""Run the actual NSIS flow with synthetic callbacks and an isolated HKCU key.

This is not a system install/uninstall or TSF compatibility test. Every file
stays in a new fixture directory; no production registration or broker call.
"""
import argparse
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
    scenarios=('check-failure','check-crash','registration-failure','rollback-failure',
               'rollback-unregister-failure','success','unicode-success','unsafe-previous','uninstall-failure')
    if a.scenario and not set(a.scenario).issubset(scenarios):parser.error('Unknown scenario')
    for scenario in scenarios:
        if a.scenario and scenario not in a.scenario:continue
        root=base/('选择位置 测试' if scenario=='unicode-success' else 'standard')/scenario
        stage=root/'stage';old=root/'old';new=root/'new';stage.mkdir(parents=True);old.mkdir()
        key_path='Software\\YANVY-Installer-Fixture\\'+uuid.uuid4().hex
        def read_key(name):
            with winreg.OpenKey(winreg.HKEY_CURRENT_USER,key_path,0,winreg.KEY_READ|winreg.KEY_WOW64_64KEY) as key:return winreg.QueryValueEx(key,name)[0]
        for folder in (stage,old):
            for name in ('YANVY-Server.exe','YANVY-Setup.exe','YANVY-Settings.exe','Uninstall.exe'):shutil.copy2(a.fixture_binary,folder/name)
            (folder/'YANVY-INSTALL-MARKER.txt').write_text('YANVY fixture\n')
        (stage/'TESTING.txt').write_text('Synthetic NSIS transaction fixture only.\n')
        (old/'settings.ini').write_text('[display]\nfont_size=23\ntheme=2\nunknown=preserve-only-in-old\n',encoding='ascii')
        (old/'keep.txt').write_text('unrelated old file must survive')
        if scenario=='unsafe-previous':(old/'YANVY-INSTALL-MARKER.txt').unlink()
        if scenario=='check-failure':(stage/'check-result.txt').write_text('1')
        if scenario=='check-crash':(stage/'check-result.txt').write_text('-1073741795')  # 0xC000001D, simulated exception status
        if scenario in ('registration-failure','rollback-failure','rollback-unregister-failure'):
            (stage/'register-result.txt').write_text('1')
        if scenario=='rollback-failure':(old/'register-result.txt').write_text('1')
        if scenario=='rollback-unregister-failure':(stage/'unregister-result.txt').write_text('1')
        (root/'stage-version.nsh').write_text('!define YANVY_VERSION "0.1.0-rc.1"\n!define YANVY_WINDOWS_VERSION "0.1.0.3"\n')
        (root/'stage-uninstall.nsh').write_text('\n'.join('Delete /REBOOTOK "$INSTDIR\\'+p.name+'"' for p in stage.iterdir())+'\n')
        source=(ROOT/'platforms/windows/installer/yanvy.nsi').read_text(encoding='utf-8-sig')
        assert PRODUCT_KEY in source
        source=source.replace(PRODUCT_KEY,key_path).replace(' HKLM ',' HKCU ')
        source=source.replace('$SMPROGRAMS',str(root/'shortcuts'))
        source=source.replace('InstallDir "$LOCALAPPDATA\\Programs\\YANVY\\${YANVY_VERSION}"','InstallDir "'+str(root/'must-not-use-default')+'"')
        icon=str(ROOT/'assets/brand/yanvy.ico')
        source=source.replace('..\\..\\..\\assets\\brand\\yanvy.ico',icon)
        script=root/'transaction.nsi';script.write_text(source,encoding='utf-8-sig')
        assert PRODUCT_KEY not in source and ' HKLM ' not in source and '$SMPROGRAMS' not in source and '$LOCALAPPDATA' not in source
        installer=root/'transaction.exe'
        subprocess.run([str(a.makensis),'/V2',f'/DSTAGE={stage}',f'/DOUTPUT={installer}',str(script)],check=True,timeout=30)
        if scenario!='rollback-unregister-failure':
            with winreg.CreateKeyEx(winreg.HKEY_CURRENT_USER,key_path,0,winreg.KEY_WRITE|winreg.KEY_WOW64_64KEY) as key:
                winreg.SetValueEx(key,'InstallLocation',0,winreg.REG_SZ,str(old));winreg.SetValueEx(key,'DisplayVersion',0,winreg.REG_SZ,'fixture-old')
        env=dict(os.environ,YANVY_SETUP_TEMP_READY='1',YANVY_PREVIOUS_CAN_CLEAN='1',TEMP=str(a.temp_dir.resolve()),TMP=str(a.temp_dir.resolve()))
        # Match the native launcher: NSIS requires /D= last and unquoted,
        # including when the selected directory contains spaces.
        install_command=subprocess.list2cmdline([str(installer)])+' /S /D='+str(new)
        result=subprocess.run(install_command,env=env,timeout=30)
        expected=0xA1260001 if scenario=='check-failure' else 0xC000001D if scenario=='check-crash' else 30 if scenario=='registration-failure' else 31 if scenario.startswith('rollback-') else 33 if scenario=='unsafe-previous' else 0
        assert result.returncode==expected,(scenario,result.returncode)
        old_trace=(old/'fixture-trace.txt').read_text() if (old/'fixture-trace.txt').exists() else ''
        if scenario in ('check-failure','check-crash'):
            assert not old_trace and read_key('InstallLocation')==str(old)
            assert (old/'YANVY-Setup.exe').exists() and (old/'Uninstall.exe').exists()
            assert not (new/'YANVY-Setup.exe').exists()
        elif scenario=='registration-failure':
            assert old_trace=='register\n' and read_key('InstallLocation')==str(old)
            assert (old/'YANVY-Setup.exe').exists() and (old/'Uninstall.exe').exists()
            assert read_key('DisplayVersion')=='fixture-old'
            assert (new/'fixture-trace.txt').read_text()=='check\nregister\nunregister\n'
        elif scenario.startswith('rollback-'):
            assert (new/'YANVY-Setup.exe').exists() and (new/'Uninstall-core.exe').exists()
            assert (new/'fixture-trace.txt').read_text()=='check\nregister\nunregister\n'
            if scenario=='rollback-failure':
                assert old_trace=='register\n' and read_key('InstallLocation')==str(old)
        elif scenario=='unsafe-previous':
            assert read_key('InstallLocation')==str(new) and (old/'YANVY-Setup.exe').exists()
            assert (new/'fixture-trace.txt').read_text()=='check\nregister\nstop\n'
        else:
            assert not old_trace and read_key('InstallLocation')==str(new)
            assert (new/'fixture-trace.txt').read_text()=='check\nregister\nstop\n'
            assert not (old/'YANVY-Setup.exe').exists() and not (old/'Uninstall.exe').exists()
            assert (old/'keep.txt').read_text()=='unrelated old file must survive'
            text=(new/'settings.ini').read_bytes()
            decoded=text.decode('utf-16') if text.startswith(b'\xff\xfe') else text.decode('utf-8-sig')
            assert 'font_size=23' in decoded and 'theme=2' in decoded and 'unknown=' not in decoded
            (new/'keep.txt').write_text('must survive exact-file removal')
            if scenario=='uninstall-failure':
                (new/'unregister-result.txt').write_text('1')
                command=subprocess.list2cmdline([str(new/'Uninstall-core.exe')])+' /S _?='+str(new)
                failed=subprocess.run(command,env=env,timeout=30)
                assert failed.returncode==30
                assert (new/'YANVY-Setup.exe').exists() and read_key('InstallLocation')==str(new)
                assert (new/'fixture-trace.txt').read_text()=='check\nregister\nstop\nunregister\n'
                (new/'unregister-result.txt').unlink()
            command=subprocess.list2cmdline([str(new/'Uninstall-core.exe')])+' /S _?='+str(new)
            removed=subprocess.run(command,env=env,timeout=30)
            assert removed.returncode in (0,3010)
            for _ in range(50):
                if not (new/'YANVY-Setup.exe').exists():break
                time.sleep(.1)
            assert (new/'keep.txt').read_text()=='must survive exact-file removal'
            assert (new/'settings.ini').exists() and not (new/'YANVY-Setup.exe').exists()
            assert (new/'fixture-trace.txt').read_text()=='check\nregister\nstop\n'+('unregister\n' if scenario=='uninstall-failure' else '')+'unregister\nstop\n'
            try:read_key('InstallLocation')
            except FileNotFoundError:pass
            else:raise AssertionError('fixture uninstall key was not removed')
        assert (old/'settings.ini').read_text().endswith('unknown=preserve-only-in-old\n')
        try:winreg.DeleteKeyEx(winreg.HKEY_CURRENT_USER,key_path,winreg.KEY_WOW64_64KEY)
        except FileNotFoundError:pass
        print(scenario+': PASS (isolated callback/registry fixture)')
    print('Fixture evidence retained at '+str(base))

if __name__=='__main__':main()
