#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
"""Synthetic real-engine IPC regression on Windows; not an app-compatibility test."""
import argparse
import ctypes as c
from ctypes import wintypes as w
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import time
import uuid

MAGIC = 0x59564E59

class Request(c.Structure):
    _fields_ = [(x, c.c_uint32) for x in ("magic", "version", "op", "key", "modifiers", "schema")] + [("owner", c.c_ubyte * 16)]

class Response(c.Structure):
    _fields_ = [(x, c.c_uint32) for x in ("magic", "version", "ok", "handled", "page", "highlighted", "count", "last_page", "cursor")] + [
        ("input", c.c_wchar * 1024), ("preedit", c.c_wchar * 1024), ("commit", c.c_wchar * 4096),
        ("candidates", (c.c_wchar * 256) * 10)]

def main():
    if os.name != "nt":
        raise SystemExit("This requires the native Windows broker.")
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument("--report", type=Path, required=True)
    args = parser.parse_args()
    k = c.WinDLL("kernel32", use_last_error=True)
    a = c.WinDLL("advapi32", use_last_error=True)
    k.GetCurrentProcess.restype = w.HANDLE
    k.CreateFileW.argtypes = [w.LPCWSTR, w.DWORD, w.DWORD, c.c_void_p, w.DWORD, w.DWORD, w.HANDLE]
    k.CreateFileW.restype = w.HANDLE
    k.ReadFile.argtypes = k.WriteFile.argtypes = [w.HANDLE, c.c_void_p, w.DWORD, c.POINTER(w.DWORD), c.c_void_p]
    k.CloseHandle.argtypes = [w.HANDLE]
    k.LocalFree.argtypes = [c.c_void_p]
    a.OpenProcessToken.argtypes = [w.HANDLE, w.DWORD, c.POINTER(w.HANDLE)]
    a.GetTokenInformation.argtypes = [w.HANDLE, c.c_int, c.c_void_p, w.DWORD, c.POINTER(w.DWORD)]
    a.ConvertSidToStringSidW.argtypes = [c.c_void_p, c.POINTER(w.LPWSTR)]
    token = w.HANDLE()
    assert a.OpenProcessToken(k.GetCurrentProcess(), 8, c.byref(token))
    size = w.DWORD()
    a.GetTokenInformation(token, 1, None, 0, c.byref(size))
    data = c.create_string_buffer(size.value)
    assert a.GetTokenInformation(token, 1, data, size, c.byref(size))
    sid = w.LPWSTR()
    assert a.ConvertSidToStringSidW(c.cast(data, c.POINTER(c.c_void_p))[0], c.byref(sid))
    session = w.DWORD()
    assert k.ProcessIdToSessionId(os.getpid(), c.byref(session))
    pipe = rf"\\.\pipe\YANVY-dev1-{sid.value}-{session.value}"
    k.LocalFree(sid)
    k.CloseHandle(token)
    checks = []

    def check(name, ok):
        if not ok:
            raise AssertionError(name)
        checks.append({"name": name, "status": "PASS"})

    def call(owner, op, key=0, schema=0, raw_version=1):
        q = Request(MAGIC, raw_version, op, key, 0, schema, (c.c_ubyte * 16).from_buffer_copy(owner.bytes_le))
        handle = w.HANDLE(-1).value
        for _ in range(100):
            handle = k.CreateFileW(pipe, 0xC0000000, 0, None, 3, 0, None)
            if handle != w.HANDLE(-1).value:
                break
            time.sleep(.02)
        if handle == w.HANDLE(-1).value:
            raise OSError(c.get_last_error(), "Broker pipe unavailable")
        try:
            count = w.DWORD()
            assert k.WriteFile(handle, c.byref(q), c.sizeof(q), c.byref(count), None)
            r = Response()
            assert k.ReadFile(handle, c.byref(r), c.sizeof(r), c.byref(count), None)
            assert count.value == c.sizeof(r)
            ack = c.c_ubyte(1)
            k.WriteFile(handle, c.byref(ack), 1, c.byref(count), None)
            return r
        finally:
            k.CloseHandle(handle)

    check("cross_arch_protocol_layout", c.sizeof(Request) == 40 and c.sizeof(Response) == 17444)
    one, two = uuid.uuid4(), uuid.uuid4()
    check("first_focus_has_no_old_composition", call(one, 1).input == "")
    for key in "wqvb":
        r = call(one, 2, ord(key))
    check("real_wubi86_candidates", [r.candidates[i].value for i in range(2)] == ["你好", "您好"])
    check("number_selects_second_candidate", call(one, 2, ord("2")).commit == "您好")
    for key in "wqvb":
        r = call(one, 2, ord(key))
    check("space_commits_once", call(one, 2, 32).commit == "你好" and call(one, 4).commit == "")
    call(one, 2, ord("w"))
    check("new_focus_discards_other_document_preedit", call(two, 1, schema=1).input == "")
    check("stale_owner_cannot_send_keys", not call(one, 2, ord("q")).ok)
    for key in "nihao":
        r = call(two, 2, ord(key), schema=1)
    check("real_pinyin_candidates", r.candidates[0].value == "你好")
    call(one, 3)
    check("old_focus_out_does_not_clear_new_focus", call(two, 4).input == "nihao")
    r = call(two, 2, 0xFF08)
    check("backspace_edits_current_owner_only", r.input == "niha")
    r = call(two, 2, 0xFF1B)
    check("escape_cancels_without_commit", r.input == "" and r.commit == "")
    for key in "nihao":
        call(two, 2, ord(key))
    r = call(two, 1, schema=0)
    check("schema_switch_cancels_pending_text", not r.input and not r.commit)
    for _ in range(10):
        for key in "wqvb":
            call(two, 2, ord(key))
        check("second_candidate_is_committed", call(two, 2, ord("2")).commit == "您好")
    for key in "wqvb":
        r = call(two, 2, ord(key))
    check("no_learning_candidate_order", r.candidates[0].value == "你好")
    call(two, 3)
    profile = args.root / "profile"
    files = [p for p in profile.rglob("*") if p.is_file()]
    marker = "YANVY_SYNTHETIC_CANARY_582931"
    for key in marker:
        call(two, 2, ord(key))
    call(two, 2, 0xFF0D)
    call(two, 3)
    files = [p for p in profile.rglob("*") if p.is_file()]
    check("no_userdb_in_broker_profile", not any("userdb" in p.name for p in profile.rglob("*")))
    check("no_body_logs_in_broker_profile", not any(p.suffix in (".log", ".jsonl", ".dmp") for p in files))
    check("synthetic_canary_not_persisted", not any(marker.encode() in p.read_bytes() for p in files))
    report = {"tested_at_utc": datetime.now(timezone.utc).isoformat(),
              "scope": "Windows native broker, local IPC and real librime. No real application or installation compatibility implied.",
              "server_sha256": hashlib.sha256((args.root / "YANVY-Server.exe").read_bytes()).hexdigest(),
              "checks": checks, "profile_files": sorted(str(p.relative_to(profile)) for p in files)}
    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.report.write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(f"{len(checks)} Windows broker checks PASS")

if __name__ == "__main__":
    main()
