#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
"""Stage a clean offline developer installer tree. No runtime/profile data copied."""
import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
from stage_data import stage
from release_version import VERSION, WINDOWS_VERSION, CHANNEL
from windows_file_version import version_value

ROOT=Path(__file__).resolve().parents[1]

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument("--build",type=Path,required=True)
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
              "current_system_upgrade_and_application_matrix":"NOT RUN; user installation and further real application verification required"}}
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
    print(a.stage.resolve())

if __name__=="__main__":main()
