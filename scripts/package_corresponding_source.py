#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
"""Bundle committed YANVY and pinned upstream sources alongside the Windows test binary."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile
import zipfile

ROOT=Path(__file__).resolve().parents[1]
PINS={".":"295cb2ab68f89ee9d3237c7d4b8033bda3f3b635",
      "deps/glog":"b33e3bad4c46c8a6345525fd822af355e5ef9446",
      "deps/leveldb":"99b3c03b3284f5886f9ef9a4ef703d57373e61be",
      "deps/marisa-trie":"0d4e8ab58eec355facf8f65ff11ef811b330e373",
      "deps/opencc":"e5d6c5f1b78e28a5797e7ad3ede3513314e544b7",
      "deps/yaml-cpp":"f7320141120f720aecc4c32be25586e7da9eb978",
      "plugins/lua":"7f3eca2ce659fc2401b8acb52bd2182b433e12b1",
      "plugins/lua/thirdparty":"9c53b362229766a97b83683b9541c46679118a90",
      "plugins/octagram":"bd12863f45fbbd5c7db06d5ec8be8987b10253bf",
      "plugins/predict":"72e4d717e56c6542569c88b317700b3471164c42"}

def head(repo):
    return subprocess.check_output(["git","rev-parse","HEAD"],cwd=repo,text=True).strip()

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument("--librime-root",type=Path,required=True)
    p.add_argument("--boost-archive",type=Path,required=True)
    p.add_argument("--temp-dir",type=Path,required=True)
    p.add_argument("--output",type=Path,required=True)
    a=p.parse_args()
    if a.output.exists():raise SystemExit("Source output already exists; choose a new file.")
    if subprocess.check_output(["git","status","--porcelain"],cwd=ROOT,text=True):
        raise SystemExit("Commit the reviewed project source first.")
    manifest=json.loads((ROOT/"third_party/windows-runtime/manifest.json").read_text(encoding="utf-8"))
    assert hashlib.sha256(a.boost_archive.read_bytes()).hexdigest()==manifest["boost_source_sha256"]
    components=[(ROOT,"yanvy-ime/",head(ROOT))]
    for relative,expected in PINS.items():
        repo=a.librime_root/relative
        assert head(repo)==expected,(relative,"source revision mismatch")
        prefix="librime/"+(relative.strip("./")+"/" if relative!="." else "")
        components.append((repo,prefix,expected))
    a.output.parent.mkdir(parents=True,exist_ok=True)
    a.temp_dir.mkdir(parents=True,exist_ok=True)
    with tempfile.TemporaryDirectory(dir=a.temp_dir,prefix="yanvy-source-") as temporary:
        with zipfile.ZipFile(a.output,"w",compression=zipfile.ZIP_DEFLATED,compresslevel=6) as out:
            for index,(repo,prefix,revision) in enumerate(components):
                archive=Path(temporary)/f"component-{index}.zip"
                subprocess.run(["git","archive","--format=zip",f"--output={archive}",revision],cwd=repo,check=True)
                with zipfile.ZipFile(archive) as src:
                    for info in src.infolist():
                        if info.is_dir():continue
                        name=info.filename
                        if ".git" in Path(name).parts or ".." in Path(name).parts or name.startswith("/"):
                            raise ValueError("Unexpected archive path")
                        info.filename=prefix+name
                        out.writestr(info,src.read(name))
            out.write(a.boost_archive,"dependencies/"+a.boost_archive.name,compress_type=zipfile.ZIP_STORED)
            records={"version":"0.1.0-dev.2","yanvy_commit":head(ROOT),"rime_sdk":manifest,
                     "components":[{"path":prefix,"commit":revision} for _,prefix,revision in components]}
            out.writestr("SOURCE-MANIFEST.json",json.dumps(records,indent=2)+"\n")
            out.writestr("README-CORRESPONDING-SOURCE.txt",
                "YANVY 0.1.0-dev.2 Windows test build corresponding sources\n\n"
                "Keep this source archive with the installer when redistributing.\n"
                "yanvy-ime/ contains all committed platform, engine adapter, installer scripts,\n"
                "data sources and original licenses. Its platform README gives build steps.\n"
                "librime/ contains Rime 1.10.0, the SDK's three optional plugin sources,\n"
                "Lua 5.4.6, and the pinned static dependency sources at their build paths.\n"
                "dependencies/ contains upstream Boost 1.84.0; extract it and set BOOST_ROOT\n"
                "when rebuilding librime with the included upstream build.bat and CMake files.\n"
                "Use the same RIME_PLUGINS listed in version-info / SOURCE-MANIFEST, or a\n"
                "separately reviewed minimal engine; YANVY itself requests only default modules.\n"
                "The original SDK was upstream's Windows clang build. Compiler/SDK versions\n"
                "may change binary bytes: bit-for-bit reproducibility is not claimed.\n"
                "All components retain their own notices and licenses.\n")
    with zipfile.ZipFile(a.output) as archive:
        assert archive.testzip() is None
        assert not any("/.git/" in name or name.endswith("/.git") for name in archive.namelist())
    print(a.output.resolve())

if __name__=="__main__":main()
