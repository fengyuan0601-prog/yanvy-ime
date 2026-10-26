#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
"""Integration tests against real librime, real upstream dictionaries, and disk."""
import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
from stage_data import stage

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, default=ROOT / "core/build/ime-core")
    parser.add_argument("--report", type=Path)
    args = parser.parse_args()
    binary = args.binary.resolve()
    if not binary.is_file():
        raise SystemExit("Build the actual core first: python3 scripts/build_core.py")
    checks = []
    def check(name, condition):
        if not condition:
            raise AssertionError(name)
        checks.append({"name": name, "status": "PASS"})
    with tempfile.TemporaryDirectory(prefix="ime-core-integration-") as temporary:
        base = Path(temporary)
        shared, user = base / "shared", base / "user"
        stage(shared, user)
        version = None
        def run(commands, profile=user, trace=False, expect_success=True):
            nonlocal version
            command = [str(binary), str(shared), str(profile)]
            if trace:
                command = ["strace", "-f", "-e", "trace=network", "-o", str(base / "network.trace")] + command
            result = subprocess.run(command, input="\n".join(commands + ["quit"]) + "\n",
                                    capture_output=True, text=True, timeout=120)
            if expect_success:
                if result.returncode != 0:
                    raise AssertionError("Real core failed: " + result.stderr)
                lines = [json.loads(x) for x in result.stdout.splitlines() if x]
                assert lines[0]["ready"] and lines[0]["engine"] == "librime"
                version = lines[0]["version"]
                assert len(lines) == len(commands) + 1
                assert not any("error" in x for x in lines), lines
                return lines[1:]
            return result

        wubi = run(["schema wubi86", "keys wqvb", "key space", "keys wq", "key backspace", "key escape"])
        check("wubi86_real_candidates", wubi[1]["candidates"][:2] == ["你好", "您好"])
        check("wubi86_space_commit", wubi[2]["commit"] == "你好" and not wubi[2]["input"])
        check("wubi86_backspace", wubi[4]["input"] == "w")
        check("escape_cancels_without_commit", not wubi[5]["input"] and not wubi[5]["commit"])

        pinyin = run(["schema pinyin_simp", "keys nihao", "key space", "keys zhongguo", "key backspace", "key escape"])
        check("pinyin_real_candidates", pinyin[1]["candidates"][0] == "你好")
        check("pinyin_phrase_commit", pinyin[2]["commit"] == "你好")
        check("pinyin_two_character_phrase", pinyin[3]["candidates"][0] == "中国")
        check("pinyin_backspace", pinyin[4]["input"] == "zhonggu")

        switch = run(["schema pinyin_simp", "keys nihao", "schema wubi86", "state"])
        check("schema_switch_cancels_pending_text", not switch[2]["input"] and not switch[2]["commit"])
        reject = subprocess.run([str(binary), str(shared), str(user)], input="schema untrusted\nquit\n",
                                capture_output=True, text=True, timeout=120)
        check("unapproved_schema_rejected", reject.returncode == 0 and '"error"' in reject.stdout)

        commands = ["schema wubi86", "keys wqvb", "clear"]
        commands += [command for _ in range(20) for command in ("keys wqvb", "key 2")]
        commands += ["keys wqvb", "clear", "keys IME_PRIVACY_CANARY_78391", "key enter"]
        learning = run(commands)
        check("repeated_second_candidate_really_committed", all(learning[4 + 2*i]["commit"] == "您好" for i in range(20)))
        check("candidate_order_stable_after_repeated_selection", learning[1]["candidates"] == learning[-4]["candidates"])
        restart = run(["schema wubi86", "keys wqvb", "clear"])
        check("candidate_order_stable_after_restart", restart[1]["candidates"] == learning[1]["candidates"])
        files = [p for p in user.rglob("*") if p.is_file()]
        check("no_user_dictionary_database_created", not any("userdb" in str(p.relative_to(user)).lower() for p in user.rglob("*")))
        marker = b"IME_PRIVACY_CANARY_78391"
        check("typed_canary_not_written_to_profile", not any(marker in p.read_bytes() for p in files))
        check("no_input_log_files_created", not any(p.suffix in (".log", ".jsonl") or p.name.startswith(("rime.ime-core", "rime.yanvy")) for p in files))

        network_trace = None
        if shutil.which("strace"):
            run(["schema pinyin_simp", "keys nihao", "key space"], trace=True)
            trace = (base / "network.trace").read_text()
            # glibc may try the local nscd UNIX socket during process setup.
            # That is local IPC, not IP traffic; do not mislabel it as a leak
            # or claim that absolutely no socket-related syscall occurred.
            network_trace = trace.splitlines()
            check("no_ip_networking_observed_during_real_input", "AF_INET" not in trace and not any(token in trace for token in ("sendto(", "sendmsg(", "bind(", "listen(")))
        else:
            checks.append({"name": "no_ip_networking_observed_during_real_input", "status": "SKIP", "reason": "strace is unavailable"})

        def inventory(directory):
            return {str(p.relative_to(directory)): hashlib.sha256(p.read_bytes()).hexdigest()
                    for p in directory.rglob("*") if p.is_file() and not p.is_symlink()}

        def attack(name, mutate, immutable=False):
            profile = base / ("attack-" + name)
            shutil.copytree(user, profile)
            mutate(profile)
            before = inventory(profile)
            result = run([], profile=profile, expect_success=False)
            check(name + "_rejected_before_session", result.returncode != 0 and '"ready":true' not in result.stdout)
            check(name + "_did_not_create_userdb", not any("userdb" in p.name.lower() for p in profile.rglob("*")))
            if immutable:
                check(name + "_did_not_modify_profile", before == inventory(profile))

        def replace_file(profile, relative, old, new):
            target = profile / relative
            text = target.read_text()
            assert old in text, relative
            target.write_text(text.replace(old, new, 1))

        attack("learning_patch", lambda p: replace_file(p, "pinyin_simp.custom.yaml", "enable_user_dict: false", "enable_user_dict: true"), immutable=True)

        alias_patch = "\n  engine/translators: [punct_translator, reverse_lookup_translator, table_translator@remember]\n  remember: {dictionary: wubi86, enable_user_dict: true, enable_encoder: true, encode_commit_history: true}\n"
        attack("named_translator_source", lambda p: (p / "wubi86.custom.yaml").write_text((p / "wubi86.custom.yaml").read_text() + alias_patch), immutable=True)

        def extra_schema(profile):
            text = (shared / "pinyin_simp.schema.yaml").read_text().replace("schema_id: pinyin_simp", "schema_id: extra")
            (profile / "extra.schema.yaml").write_text(text)
            replace_file(profile, "default.custom.yaml", "schema_list:", "schema_list:\n    - schema: extra")
        attack("unapproved_initial_schema", extra_schema, immutable=True)

        def add_script(profile):
            (profile / "rime.lua").write_text("-- Unauthorized executable configuration, must never be evaluated.\n")
        attack("unexpected_lua_file", add_script, immutable=True)

        # These attacks leave all source/patch hashes correct. They verify the
        # independent effective-configuration gate against altered build caches.
        def cached_alias(profile):
            replace_file(profile, "build/wubi86.schema.yaml", "    - table_translator\n", "    - table_translator@remember\n")
            file = profile / "build/wubi86.schema.yaml"
            file.write_text(file.read_text() + "\nremember:\n  dictionary: wubi86\n  enable_user_dict: true\n  enable_encoder: true\n  encode_commit_history: true\n")
        attack("named_translator_compiled", cached_alias)
        attack("compiled_initial_schema_list", lambda p: replace_file(p, "build/default.yaml", "  - schema: wubi86", "  - schema: extra"))
        attack("compiled_lua_filter", lambda p: replace_file(p, "build/wubi86.schema.yaml", "engine:\n", "engine:\n  filters: [lua_filter@record_input]\n"))
        attack("compiled_keybinding_schema_jump", lambda p: replace_file(p, "build/wubi86.schema.yaml", "select: .next", "select: extra"))
        attack("compiled_dictionary_redirect", lambda p: replace_file(p, "build/wubi86.schema.yaml", "dictionary: wubi86", "dictionary: pinyin_simp"))

        if hasattr(os, "symlink") and sys.platform != "win32":
            def symlink_patch(profile):
                destination = profile / "pinyin_simp.custom.yaml"
                destination.unlink()
                destination.symlink_to(user / "pinyin_simp.custom.yaml")
            attack("symlinked_privacy_patch", symlink_patch, immutable=True)

        report = {"tested_at_utc": datetime.now(timezone.utc).isoformat(),
                  "scope": "Native Linux librime core only; no Windows TSF/mobile integration was tested.",
                  "engine": "librime", "engine_version": version,
                  "binary_sha256": hashlib.sha256(binary.read_bytes()).hexdigest(),
                  "learning": False, "checks": checks, "network_trace": network_trace,
                  "profile_files": [str(p.relative_to(user)) for p in files],
                  "privacy_limit": "These checks cover the exercised build and scenarios, not a security certification or protection against the OS/host application."}
        if args.report:
            args.report.parent.mkdir(parents=True, exist_ok=True)
            args.report.write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n")
        print(json.dumps(report, ensure_ascii=False, indent=2))

if __name__ == "__main__":
    main()
