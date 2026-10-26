#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
"""One version source for CMake, binary metadata, installer and source archive."""
import json
from pathlib import Path
import re

ROOT=Path(__file__).resolve().parents[1]
_data=json.loads((ROOT/'version.json').read_text(encoding='utf-8'))
VERSION=_data['version']
WINDOWS_VERSION=_data['windows_version']
CHANNEL=_data['channel']
assert re.fullmatch(r'\d+\.\d+\.\d+(?:-(?:dev|rc)\.\d+)?',VERSION)
assert re.fullmatch(r'\d+\.\d+\.\d+\.\d+',WINDOWS_VERSION)
assert CHANNEL in ('development','release-candidate','stable')
assert all(int(n)<=65535 for n in WINDOWS_VERSION.split('.'))
