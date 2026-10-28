#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
"""Exercise the ordinary-user data payload; no TSF, UAC, registry or brokers."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import uuid

ROOT = Path(__file__).resolve().parents[1]
COMMIT = "1234567890abcdef1234567890abcdef12345678"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--makensis", type=Path, required=True)
    parser.add_argument("--temp-dir", type=Path, required=True)
    a = parser.parse_args()
    base = a.temp_dir.resolve() / ("yanvy-user-data-" + uuid.uuid4().hex)
    assert base.is_relative_to(a.temp_dir.resolve()) and not base.exists()
    base.mkdir(parents=True)
    stage = base / "stage"
    files = {
        "data/default.yaml": "schema_list: []\n",
        "data/test.dict.yaml": "# synthetic fixture dictionary\n",
        "profile/default.custom.yaml": "patch: {}\n",
        "licenses/INDEX.md": "Synthetic fixture only.\n",
        "licenses/component/LICENSE": "Synthetic license fixture.\n",
        "TESTING.txt": "Synthetic data-only payload.\n",
        "BUILD-INFO.json": json.dumps({"source_commit": COMMIT}) + "\n",
        "YANVY-INSTALL-MARKER.txt": "YANVY 0.1.0-fixture\n" + COMMIT + "\n",
    }
    for name, content in files.items():
        path = stage / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(content, encoding="utf-8")
    # These must not be copied even if a staging directory accidentally has them.
    for name in ("data/hidden.exe", "profile/hidden.dll", "licenses/component/hidden.exe", "YANVY-Server.exe"):
        (stage / name).write_bytes(b"synthetic executable-name canary")
    Path(str(stage) + "-version.nsh").write_text(
        '!define YANVY_VERSION "0.1.0-fixture"\n!define YANVY_WINDOWS_VERSION "0.1.0.0"\n', encoding="utf-8")
    cleanup = base / "data-uninstall.nsh"
    lines = ['Delete "$INSTDIR\\' + name.replace("/", "\\") + '"' for name in files if name != "YANVY-INSTALL-MARKER.txt"]
    lines += ['IfErrors 0 +2', 'StrCpy $CleanupFileError 1', 'ClearErrors']
    lines += ['RMDir "$INSTDIR\\' + name + '"' for name in ("licenses\\component", "licenses", "profile", "data")]
    lines += ['ClearErrors']
    cleanup.write_text("\n".join(lines) + "\n", encoding="utf-8")
    source = (ROOT / "platforms/windows/installer/user_data.nsi").read_text(encoding="utf-8-sig")
    env = dict(os.environ, TEMP=str(a.temp_dir.resolve()), TMP=str(a.temp_dir.resolve()))

    def build(name, content):
        script = base / (name + ".nsi")
        script.write_text(content, encoding="utf-8-sig")
        output = base / (name + ".exe")
        subprocess.run([str(a.makensis.resolve()), "/V2", f"/DSTAGE={stage}", f"/DOUTPUT={output}",
                        f"/DCLEANUP={cleanup}", f"/DSOURCE_COMMIT={COMMIT}", str(script)],
                       env=env, check=True, timeout=60)
        return output

    payload = build("data-payload", source)

    def run(executable, target, expected, remove=False):
        assert target.resolve().is_relative_to(base.resolve())
        command = subprocess.list2cmdline([str(executable)]) + " /S" + (" /CLEANUP" if remove else "") + " /D=" + str(target)
        result = subprocess.run(command, env=env, timeout=30)
        assert result.returncode == expected, (target.name, remove, result.returncode, expected)

    target = base / "中文 选择目录" / "词库"
    run(payload, target, 0)
    for name, content in files.items():
        assert (target / name).read_text(encoding="utf-8") == content, name
    assert not any(p.suffix.lower() in (".exe", ".dll") for p in target.rglob("*"))
    print("unicode/spaces, exact data and no executable payload: PASS", flush=True)
    unknown = target / "licenses/component/user-file.txt"
    unknown.write_text("preserve", encoding="utf-8")
    run(payload, target, 10)
    run(payload, target, 0, remove=True)
    assert unknown.read_text(encoding="utf-8") == "preserve"
    assert all(not (target / name).exists() for name in files)
    run(payload, target, 10, remove=True)
    assert unknown.exists()
    print("refuse overwrite; cleanup preserves unknown and refuses missing marker: PASS", flush=True)

    empty = base / "empty directory"
    empty.mkdir()
    run(payload, empty, 0)
    run(payload, empty, 0, remove=True)
    assert not empty.exists() or not any(empty.iterdir())
    run(payload, empty, 0)
    print("existing empty directory and same-path reinstall: PASS", flush=True)
    (empty / "YANVY-INSTALL-MARKER.txt").write_text("YANVY 0.1.0-fixture\nwrong-commit\n", encoding="utf-8")
    run(payload, empty, 10, remove=True)
    assert (empty / "data/default.yaml").exists()
    print("mismatched source marker refuses cleanup: PASS", flush=True)

    # Exercise both fail-closed branches without requesting administrator
    # rights. Only fixture copies receive a synthetic token-query outcome.
    token_sentinel = "; TEST_POINT_AFTER_ELEVATION_QUERY"
    assert source.count(token_sentinel) == 1
    for token_case, injected in (("elevated-token", "StrCpy $2 1"),
                                 ("unreadable-token", "StrCpy $1 0")):
        guarded = build(token_case, source.replace(token_sentinel, injected))
        guarded_target = base / token_case
        run(guarded, guarded_target, 40)
        assert not guarded_target.exists()
        before = (empty / "YANVY-INSTALL-MARKER.txt").read_bytes()
        run(guarded, empty, 40, remove=True)
        assert (empty / "YANVY-INSTALL-MARKER.txt").read_bytes() == before
    print("elevated/unknown token denied before install or cleanup: PASS (injected outcomes)", flush=True)

    old = base / "old unrelated directory"
    old.mkdir()
    (old / "keep.txt").write_text("old", encoding="utf-8")
    run(payload, old, 10)
    assert list(p.name for p in old.iterdir()) == ["keep.txt"]
    print("old unrelated nonempty directory untouched: PASS", flush=True)

    # Simulate extraction failure after writing known files. This source-only
    # injection is not present in distributable payloads.
    sentinel = "; TEST_POINT_AFTER_EXTRACTION"
    assert source.count(sentinel) == 1
    failed = build("failure-payload", source.replace(sentinel,
        'FileOpen $0 "$INSTDIR\\unknown-canary.txt" w\nFileWrite $0 "preserve"\nFileClose $0\nGoto install_failed'))
    failure_target = base / "failed extraction"
    run(failed, failure_target, 21)
    assert (failure_target / "unknown-canary.txt").read_text() == "preserve"
    assert all(not (failure_target / name).exists() for name in files)
    print("failure removes only exact payload files: PASS", flush=True)

    junction_root = base / "junction test"
    run(payload, junction_root, 0)
    linked = junction_root / "data"
    outside = base / "outside data"
    assert linked.resolve().is_relative_to(base.resolve()) and outside.resolve().is_relative_to(base.resolve())
    linked.rename(outside)
    link_env = dict(env, YANVY_TEST_LINK=str(linked), YANVY_TEST_TARGET=str(outside))
    try:
        subprocess.run(["powershell.exe", "-NoProfile", "-NonInteractive", "-Command",
                        "$ErrorActionPreference='Stop'; New-Item -ItemType Junction -Path $env:YANVY_TEST_LINK -Target $env:YANVY_TEST_TARGET | Out-Null"],
                       env=link_env, check=True, timeout=30, capture_output=True)
        run(payload, junction_root, 10, remove=True)
        assert (outside / "default.yaml").read_text(encoding="utf-8") == files["data/default.yaml"]
        assert (junction_root / "YANVY-INSTALL-MARKER.txt").exists()
    finally:
        if linked.exists():
            os.rmdir(linked)  # Remove only the junction, never its target.
    print("junction cleanup refused without changing target: PASS", flush=True)
    print("Fixture evidence retained at " + str(base))


if __name__ == "__main__":
    main()
