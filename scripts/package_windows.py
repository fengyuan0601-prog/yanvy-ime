#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
"""Stage a clean offline developer installer tree. No runtime/profile data copied."""
import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import shutil
import struct
import subprocess
from stage_data import stage
from release_version import VERSION, WINDOWS_VERSION, CHANNEL
from windows_file_version import version_value

ROOT=Path(__file__).resolve().parents[1]

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument("--build",type=Path,required=True)
    p.add_argument("--build-x86",type=Path,required=True)
    p.add_argument("--rime-sdk",type=Path,required=True)
    p.add_argument("--stage",type=Path,required=True)
    a=p.parse_args()
    commit=subprocess.check_output(["git","rev-parse","HEAD"],cwd=ROOT,text=True).strip()
    if subprocess.check_output(["git","status","--porcelain"],cwd=ROOT,text=True):
        raise SystemExit("Commit reviewed source before staging a distributable package.")
    if a.stage.exists() and any(a.stage.iterdir()):
        raise SystemExit("Use a new empty package staging directory.")
    subprocess.run([__import__('sys').executable,str(ROOT/"scripts/generate_policy.py")],check=True)
    dll=a.rime_sdk/"dist/lib/rime.dll"
    from fetch_windows_sdk import DLL_SHA256
    assert hashlib.sha256(dll.read_bytes()).hexdigest()==DLL_SHA256
    a.stage.mkdir(parents=True,exist_ok=True)
    for file in ("YANVY-TSF.dll","YANVY-Server.exe","YANVY-Setup.exe","YANVY-Settings.exe","Uninstall.exe"):
        if version_value(a.build/file,'SourceCommit')!=commit or version_value(a.build/file,'FileVersion')!=VERSION:
            raise SystemExit(f"Reconfigure and rebuild {file}: its source commit or version differs from HEAD.")
        shutil.copy2(a.build/file,a.stage/file)
    x86=a.build_x86/"YANVY-TSF.dll"
    if version_value(x86,'SourceCommit')!=commit or version_value(x86,'FileVersion')!=VERSION:
        raise SystemExit("Reconfigure and rebuild the x86 TSF DLL from the current commit.")
    pe=x86.read_bytes();offset=struct.unpack_from('<I',pe,0x3c)[0]
    if pe[offset:offset+4]!=b'PE\0\0' or struct.unpack_from('<H',pe,offset+4)[0]!=0x014c:
        raise SystemExit("The x86 TSF DLL is not a 32-bit Windows binary.")
    shutil.copy2(x86,a.stage/"YANVY-TSF-x86.dll")
    x86_probe=a.build_x86/"YANVY-COM-Architecture-Fixture.exe"
    if version_value(x86_probe,'SourceCommit')!=commit or version_value(x86_probe,'FileVersion')!=VERSION:
        raise SystemExit("Reconfigure and rebuild the x86 COM check from the current commit.")
    pe=x86_probe.read_bytes();offset=struct.unpack_from('<I',pe,0x3c)[0]
    if pe[offset:offset+4]!=b'PE\0\0' or struct.unpack_from('<H',pe,offset+4)[0]!=0x014c:
        raise SystemExit("The x86 COM check is not a 32-bit Windows binary.")
    shutil.copy2(x86_probe,a.stage/"YANVY-Check-x86.exe")
    shutil.copy2(dll,a.stage/"rime.dll")
    stage(a.stage/"data",a.stage/"profile")
    licenses=a.stage/"licenses"
    licenses.mkdir()
    shutil.copy2(ROOT/"LICENSE",licenses/"YANVY-BSD-3-Clause.txt")
    shutil.copy2(ROOT/"LICENSING.md",licenses/"INDEX.md")
    for directory in (ROOT/"third_party").iterdir():
        if directory.is_dir():
            for file in directory.iterdir():
                if file.is_file() and (file.name.startswith(("LICENSE","COPYING","AUTHORS","GCC-")) or file.suffix in (".txt",".json",".md")):
                    target=licenses/directory.name/file.name
                    target.parent.mkdir(exist_ok=True)
                    shutil.copy2(file,target)
    shutil.copy2(ROOT/"third_party/GPL-3.0.txt",licenses/"GPL-3.0.txt")
    shutil.copy2(ROOT/"platforms/windows/TESTING.txt",a.stage/"TESTING.txt")
    dirty=bool(subprocess.check_output(["git","status","--porcelain"],cwd=ROOT,text=True))
    if dirty or subprocess.check_output(["git","rev-parse","HEAD"],cwd=ROOT,text=True).strip()!=commit:
        raise SystemExit("Commit the reviewed sources before creating a distributable package.")
    records={str(f.relative_to(a.stage)).replace("\\","/"):hashlib.sha256(f.read_bytes()).hexdigest()
             for f in a.stage.rglob("*") if f.is_file()}
    info={"version":VERSION,"channel":CHANNEL,"architecture":"x64","unsigned":True,
          "source_commit":commit,"source_url":f"https://github.com/fengyuan0601-prog/yanvy-ime/tree/{commit}",
          "created_at_utc":datetime.now(timezone.utc).isoformat(),"rime":"1.10.0",
          "files_sha256":records,
          "verification_scope":{
              "dev1_usage":"Maintainer reports installed dev.1 is being used for real input; no complete application matrix",
              "dev2_ui":"Native settings save/reopen/defaults and 18/32 font preview checked; painter rendered at 96/144/192 DPI",
              "rc1":"65 core PASS/1 network-trace SKIP; 35 isolated v2 broker PASS; new NSIS transaction/path and preference fixtures PASS; native UI displayed",
              "rc2":"65 core PASS/1 network-trace SKIP; 35 broker PASS; 18 ordered-edit checks; 6 isolated NSIS scenarios; 4 registration-removal failure injections and retries; 48 bounded candidate layouts; 15 paths; preferences and COM smoke PASS. See docs/RC2-READINESS.md for scope",
              "rc3":"caret geometry failure/clipping checks and candidate layout fixture; installed rc2 files verified and rc2 core rechecked; real rc3 installation and application tests pending. See docs/RC3-READINESS.md",
              "rc4":"15 path boundaries, synthetic no-E fallback, 7 isolated NSIS cases including Unicode/space, 35 v3 broker checks, native tray add/remove smoke; real installation and tray menu interaction remain unverified. See docs/RC4-READINESS.md",
              "rc5":"real librime and isolated NSIS checks cover valid data, corrupted data and missing rime DLL; path-deduplication and failure categorization. User-reported target computer installation not yet verified. See docs/RC5-READINESS.md",
              "rc6":"16 path checks; 35 broker checks with a simulated protected install directory; isolated user preference and tamper checks; 8 NSIS synthetic upgrade/rollback and 4 real-engine NSIS cases. Actual UAC installation and migration remain unverified. See docs/RC6-READINESS.md",
              "rc7":"Notification-icon version negotiation and event decoding, synthetic upgrade transaction, 35 isolated broker checks, and path checks. Actual taskbar interaction and system upgrade remain unverified. See docs/RC7-READINESS.md",
              "rc8":"Private real TSF compartment mode round trips; Windows real application hotkey interaction remains unverified. See docs/RC8-READINESS.md",
              "rc9":"Legacy named-pipe compatibility and isolated x86 COM, mode and real-engine IPC tests. Real 32-bit and other application acceptance remains pending. See docs/RC9-READINESS.md",
              "0.1.0_system_upgrade":"2026-10-29: rc.9 to 0.1.0 real administrator upgrade passed; 74 installed file hashes matched; x64/x86 COM and registration passed; one new broker; old profile retired. User confirmed Codex Chinese and Ctrl+Space round trips. User then reported tray menu displacement; do not treat that menu as accepted.",
              "0.1.1_system_upgrade":"2026-10-29: real upgrade passed, 74 installed file hashes matched, current-version broker ownership verified; user confirmed menu placement. User then requested native brand plus Chinese/English mode button instead of a duplicate notification icon.",
              "0.1.2_system_upgrade":"2026-10-29: installed fixed files matched all 74 staged hashes. User reported native button or clicks still failed and repeating the installer rejected the populated data directory; do not treat these interactions as accepted.",
              "0.1.3_system_upgrade":"Installed fixed files matched all 74 staged hashes; profile, x64/x86 COM and new broker ownership passed. User confirmed the mode button left click works, but right click did not open settings. Retained 0.1.1/0.1.2 TSF modules referenced settings executables removed by upgrade cleanup.",
              "0.1.4_upgrade_implementation":"0.1.4 restores missing known protected settings entry points and forwards them to the current runtime. A fixed settings action can be handled by the authenticated ordinary desktop broker; failures return HRESULT without input data. x64/x86 launch, compatibility and mode regressions passed; actual 0.1.4 installation and right-click acceptance were subsequently confirmed; see release audit. Private local TSF identity auto-instantiation fixture did not activate on this host; keep that failure separate from actual application results. No universal application compatibility claim",
              "0.1.6_startup":"Token policy and actual ordinary Explorer launches checked on x64/x86; startup/data/machine/user stages and elevation/UAC diagnostics added. UAC-disabled admin desktop cannot supply an ordinary token. Target-device installation pending. See docs/INSTALLER-0.1.6.md.",
              "0.1.5_ui":"Native settings window: scheme selection, font size 32, dark theme, save/reopen and defaults verified in an isolated user folder. Eighteen regression groups passed, including 68 core checks and one network trace skip. Installation UI, paths and native scale/state renders checked. Actual 0.1.5 UAC upgrade and installed taskbar/application acceptance remain unverified. See docs/UI-0.1.5.md."}}
    (a.stage/"BUILD-INFO.json").write_text(json.dumps(info,ensure_ascii=False,indent=2)+"\n",encoding="utf-8")
    (a.stage/"YANVY-INSTALL-MARKER.txt").write_text("YANVY "+VERSION+"\n"+commit+"\n",encoding="utf-8")
    (a.stage.parent/(a.stage.name+"-version.nsh")).write_text(
        f'!define YANVY_VERSION "{VERSION}"\n!define YANVY_WINDOWS_VERSION "{WINDOWS_VERSION}"\n',encoding='utf-8')
    files=sorted((f for f in a.stage.rglob("*") if f.is_file()),key=lambda f:str(f))
    lines=['; Generated exact-file removal list. Never recursively delete a user-selected directory.']
    lines += ['Delete /REBOOTOK "$INSTDIR\\'+str(f.relative_to(a.stage)).replace('/','\\')+'"' for f in files]
    directories=sorted((d for d in a.stage.rglob("*") if d.is_dir()),key=lambda d:len(d.parts),reverse=True)
    lines += ['RMDir "$INSTDIR\\'+str(d.relative_to(a.stage)).replace('/','\\')+'"' for d in directories]
    (a.stage.parent/(a.stage.name+"-uninstall.nsh")).write_text("\n".join(lines)+"\n",encoding="utf-8")
    # This list runs exclusively under the original user's token. Never mix
    # executable/runtime files into the freely selected dictionary location.
    denied={'.exe','.dll','.com','.bat','.cmd','.ps1','.vbs','.js'}
    def in_user_payload(f):
        relative=f.relative_to(a.stage)
        return ((relative.parts[0] in ('data','profile') and len(relative.parts)==2 and f.suffix.lower()=='.yaml')
                or (relative.parts[0]=='licenses' and f.suffix.lower() not in denied)
                or (len(relative.parts)==1 and f.name in ('TESTING.txt','BUILD-INFO.json')))
    data_files=[f for f in files if in_user_payload(f)]
    # Embed this independent allowlist in the public installer. Never trust an
    # existing destination's BUILD-INFO.json to authorize privileged reuse.
    data_set=set(data_files)|{a.stage/"YANVY-INSTALL-MARKER.txt"}
    manifest_lines=['YANVY-PACKAGE-MANIFEST 1']
    manifest_lines += [('D' if f in data_set else 'R')+'\t'+hashlib.sha256(f.read_bytes()).hexdigest()+'\t'+
                       f.relative_to(a.stage).as_posix() for f in files]
    with (a.stage.parent/(a.stage.name+"-package-manifest.txt")).open('w',encoding='utf-8',newline='\n') as manifest_file:
        manifest_file.write("\n".join(manifest_lines)+"\n")
    data_lines=['; Exact user-data cleanup; marker is checked and removed by user_data.nsi.']
    data_lines += ['Delete "$INSTDIR\\'+str(f.relative_to(a.stage)).replace('/','\\')+'"' for f in data_files]
    data_lines += ['IfErrors 0 +2','StrCpy $CleanupFileError 1','ClearErrors']
    data_dirs={parent for f in data_files for parent in f.parents if parent!=a.stage and a.stage in parent.parents}
    data_lines += ['RMDir "$INSTDIR\\'+str(d.relative_to(a.stage)).replace('/','\\')+'"'
                   for d in sorted(data_dirs,key=lambda d:len(d.parts),reverse=True)]
    data_lines += ['ClearErrors']
    (a.stage.parent/(a.stage.name+"-data-uninstall.nsh")).write_text("\n".join(data_lines)+"\n",encoding="utf-8")
    print(a.stage.resolve())

if __name__=="__main__":main()
