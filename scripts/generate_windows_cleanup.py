#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
"""Generate the compiled, bounded ordinary-user cleanup list from repository inputs."""
import argparse
from pathlib import Path, PurePosixPath
from stage_data import FILES, ROOT

DATA_EXCLUDED_SUFFIXES = {".exe", ".dll", ".com", ".bat", ".cmd", ".ps1", ".vbs", ".js"}


def cleanup_files():
    data = {"data/" + name for names in FILES.values() for name in names}
    data.update("profile/" + path.name for path in (ROOT / "schemas").glob("*.custom.yaml"))
    data.update({"licenses/YANVY-BSD-3-Clause.txt", "licenses/INDEX.md", "licenses/GPL-3.0.txt",
                 "TESTING.txt", "BUILD-INFO.json"})
    for directory in (ROOT / "third_party").iterdir():
        if directory.is_dir():
            for path in directory.iterdir():
                if path.is_file() and path.suffix.lower() not in DATA_EXCLUDED_SUFFIXES and (path.name.startswith(("LICENSE", "COPYING", "AUTHORS", "GCC-"))
                                       or path.suffix in (".txt", ".json", ".md")):
                    data.add(f"licenses/{directory.name}/{path.name}")
    runtime = {"YANVY-TSF.dll", "YANVY-TSF-x86.dll", "YANVY-Server.exe", "YANVY-Setup.exe",
               "YANVY-Settings.exe", "YANVY-Check-x86.exe", "Uninstall.exe", "Uninstall-core.exe",
               "rime.dll", "installation.ini"}
    cache = {"profile/user.yaml", "profile/installation.yaml", "profile/build/default.yaml"}
    for schema in ("wubi86", "pinyin_simp", "stroke", "double_pinyin", "double_pinyin_flypy", "double_pinyin_mspy"):
        cache.update(f"profile/build/{schema}.{extension}" for extension in
                     ("schema.yaml", "table.bin", "prism.bin", "reverse.bin"))
    directories = set()
    for name in data | cache:
        parent = PurePosixPath(name).parent
        while parent != PurePosixPath("."):
            directories.add(str(parent))
            parent = parent.parent
    for name in data | runtime | cache | directories:
        path = PurePosixPath(name)
        if path.is_absolute() or any(part in ("", ".", "..") for part in path.parts) or any(c in name for c in '\\:"\r\n'):
            raise ValueError(f"Unsafe compiled cleanup path: {name!r}")
    return data, runtime, cache, directories


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    groups = cleanup_files()
    lines = ["// Generated from reviewed repository inputs; never from an installed directory.",
             "#pragma once", "namespace yanvy {"]
    for symbol, group in zip(("kCleanupDataFiles", "kCleanupRuntimeFiles", "kCleanupCacheFiles", "kCleanupDirectories"), groups):
        ordered = sorted(group, key=lambda name: (-len(PurePosixPath(name).parts), name))
        lines.append(f"inline constexpr const wchar_t* {symbol}[] = {{")
        lines.extend('  L"' + name.replace("/", "\\\\") + '",' for name in ordered)
        lines.append("};")
    lines.append("}")
    args.output.parent.mkdir(parents=True, exist_ok=True)
    text = "\n".join(lines) + "\n"
    if not args.output.exists() or args.output.read_text(encoding="utf-8") != text:
        args.output.write_text(text, encoding="utf-8")


if __name__ == "__main__":
    main()
