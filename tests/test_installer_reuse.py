#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
"""Exercise only the unelevated data worker in fresh directories. No UAC or registration."""
import argparse
import ctypes
import hashlib
from pathlib import Path
import subprocess
import uuid


def snapshot(root):
    return {p.relative_to(root).as_posix(): hashlib.sha256(p.read_bytes()).hexdigest()
            for p in root.rglob('*') if p.is_file()}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, required=True)
    parser.add_argument('--temp-dir', type=Path, required=True)
    args = parser.parse_args()
    base = args.temp_dir.resolve() / ('yanvy-data-reuse-' + uuid.uuid4().hex)
    assert base.is_relative_to(args.temp_dir.resolve()) and not base.exists()
    base.mkdir(parents=True)
    target = base / '中文 词库'

    def prepare(path):
        return subprocess.run([str(args.binary.resolve()), '--worker-prepare-data', str(path)], timeout=60).returncode

    result = prepare(target)
    if ctypes.windll.shell32.IsUserAnAdmin():
        assert result == 740 and not target.exists(), 'elevated data worker must reject before writing'
        print('Elevated data-worker rejection PASS; medium-token reuse scenarios SKIP')
        return
    assert result == 0, ('new data extraction', result)
    files = snapshot(target)
    assert files and 'YANVY-INSTALL-MARKER.txt' in files
    assert not any(Path(name).suffix.lower() in ('.exe', '.dll') for name in files)
    (target / 'personal.txt').write_text('preserve this unrelated file', encoding='utf-8')
    (target / 'licenses' / 'personal.txt').write_text('preserve this extra license note', encoding='utf-8')
    before = snapshot(target)
    assert prepare(target) == 12 and snapshot(target) == before, 'repeat must reuse exact existing data without mutation'
    assert prepare(target) == 12 and snapshot(target) == before, 'repeated reuse must remain idempotent'
    for relative in ('YANVY-INSTALL-MARKER.txt', 'BUILD-INFO.json', 'profile/default.custom.yaml'):
        file = target / relative
        original = file.read_bytes()
        file.write_bytes(b'tampered fixture bytes')
        altered = snapshot(target)
        assert prepare(target) == 42 and snapshot(target) == altered, ('tampered data must be retained', relative)
        file.write_bytes(original)
    missing = target / 'profile/default.custom.yaml'
    original = missing.read_bytes()
    missing.unlink()
    altered = snapshot(target)
    assert prepare(target) == 42 and snapshot(target) == altered, 'missing fixed data is not silently accepted'
    missing.write_bytes(original)
    extra = target / 'data/unknown.custom.yaml'
    extra.write_text('unexpected schema policy', encoding='utf-8')
    altered = snapshot(target)
    assert prepare(target) == 42 and snapshot(target) == altered, 'extra input data is rejected without mutation'
    extra.unlink()
    second = base / '新位置'
    assert prepare(second) == 0 and snapshot(target) == before, 'new data location must leave old data intact'
    assert snapshot(second) == files
    print('Actual data worker PASS: fresh extraction, exact-package repeat, unknown-file preservation, tampering/missing/extra-input rejection and independent new location; no machine install')
    print(base)


if __name__ == '__main__':
    main()
