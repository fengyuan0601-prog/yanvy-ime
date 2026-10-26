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

ROOT=Path(__file__).resolve().parents[1]

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument("--build",type=Path,required=True)
    p.add_argument("--rime-sdk",type=Path,required=True)
    p.add_argument("--stage",type=Path,required=True)
    a=p.parse_args()
    if a.stage.exists() and any(a.stage.iterdir()):
        raise SystemExit("Use a new empty package staging directory.")
    subprocess.run([__import__('sys').executable,str(ROOT/"scripts/generate_policy.py")],check=True)
    dll=a.rime_sdk/"dist/lib/rime.dll"
    from fetch_windows_sdk import DLL_SHA256
    assert hashlib.sha256(dll.read_bytes()).hexdigest()==DLL_SHA256
    a.stage.mkdir(parents=True,exist_ok=True)
    for file in ("YANVY-TSF.dll","YANVY-Server.exe","YANVY-Setup.exe","YANVY-Settings.exe","Uninstall.exe"):
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
    commit=subprocess.check_output(["git","rev-parse","HEAD"],cwd=ROOT,text=True).strip()
    dirty=bool(subprocess.check_output(["git","status","--porcelain"],cwd=ROOT,text=True))
    if dirty:
        raise SystemExit("Commit the reviewed sources before creating a distributable package.")
    records={str(f.relative_to(a.stage)).replace("\\","/"):hashlib.sha256(f.read_bytes()).hexdigest()
             for f in a.stage.rglob("*") if f.is_file()}
    info={"version":"0.1.0-dev.1","architecture":"x64","unsigned":True,
          "source_commit":commit,"source_url":f"https://github.com/fengyuan0601-prog/yanvy-ime/tree/{commit}",
          "created_at_utc":datetime.now(timezone.utc).isoformat(),"rime":"1.10.0",
          "files_sha256":records,"system_registration_and_real_app_tests":"NOT RUN: require Windows administrator authorization"}
    (a.stage/"BUILD-INFO.json").write_text(json.dumps(info,ensure_ascii=False,indent=2)+"\n",encoding="utf-8")
    (a.stage/"YANVY-INSTALL-MARKER.txt").write_text("YANVY 0.1.0-dev.1\n"+commit+"\n",encoding="utf-8")
    files=sorted((f for f in a.stage.rglob("*") if f.is_file()),key=lambda f:str(f))
    lines=['; Generated exact-file removal list. Never recursively delete a user-selected directory.']
    lines += ['Delete /REBOOTOK "$INSTDIR\\'+str(f.relative_to(a.stage)).replace('/','\\')+'"' for f in files]
    directories=sorted((d for d in a.stage.rglob("*") if d.is_dir()),key=lambda d:len(d.parts),reverse=True)
    lines += ['RMDir "$INSTDIR\\'+str(d.relative_to(a.stage)).replace('/','\\')+'"' for d in directories]
    (a.stage.parent/(a.stage.name+"-uninstall.nsh")).write_text("\n".join(lines)+"\n",encoding="utf-8")
    print(a.stage.resolve())

if __name__=="__main__":main()
