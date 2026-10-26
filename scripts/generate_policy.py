#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
"""Verify build inputs, or explicitly refresh the checked-in runtime trust policy."""
import argparse
import hashlib
import json
from pathlib import Path
from stage_data import FILES, ROOT

def expected_header():
    lock = json.loads((ROOT / "third_party/manifest.lock.json").read_text())
    records = {x["name"]: x for x in lock["sources"]}
    shared = []
    for repo, names in FILES.items():
        for name in names:
            actual = hashlib.sha256((ROOT / "third_party" / repo / name).read_bytes()).hexdigest()
            if actual != records[repo]["files"][name]:
                raise ValueError(f"Untrusted upstream source: {repo}/{name}")
            shared.append((name, actual))
    for name, expected in records["picosha2"]["files"].items():
        if hashlib.sha256((ROOT / "third_party/picosha2" / name).read_bytes()).hexdigest() != expected:
            raise ValueError("Untrusted hashing dependency")
    patches = [(p.name, hashlib.sha256(p.read_bytes()).hexdigest())
               for p in sorted((ROOT / "schemas").glob("*.custom.yaml"))]
    lines = ["// SPDX-License-Identifier: BSD-3-Clause", "// Generated only by an explicit reviewed policy update; never from runtime data.",
             "#pragma once", "namespace ime::policy {", "struct Input { const char* name; const char* sha256; };"]
    for label, entries in (("kShared", sorted(shared)), ("kUser", patches)):
        lines += [f"inline constexpr Input {label}[] = {{"]
        lines += [f'  {{"{name}", "{digest}"}},' for name, digest in entries]
        lines += ["};"]
    lines += ["}  // namespace ime::policy", ""]
    return "\n".join(lines)

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--update", action="store_true", help="Explicit developer action after reviewing source/patch changes")
    args = parser.parse_args()
    target = ROOT / "core/trusted_inputs.hpp"
    text = expected_header()
    if args.update:
        target.write_text(text)
        print("Updated compiled trust policy; rebuild and rerun adversarial tests.")
    elif not target.exists() or target.read_text() != text:
        raise SystemExit("Trust policy differs from source. Review changes before running generate_policy.py --update.")
    else:
        print("Pinned source files and compiled trust policy verified.")

if __name__ == "__main__":
    main()
