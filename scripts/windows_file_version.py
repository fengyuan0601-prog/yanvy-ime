#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
"""Read a PE version string through Windows; never execute the inspected file."""
import ctypes as c
from ctypes import wintypes as w

def version_value(path,key):
    api=c.WinDLL('version',use_last_error=True)
    api.GetFileVersionInfoSizeW.argtypes=[w.LPCWSTR,c.POINTER(w.DWORD)];api.GetFileVersionInfoSizeW.restype=w.DWORD
    api.GetFileVersionInfoW.argtypes=[w.LPCWSTR,w.DWORD,w.DWORD,c.c_void_p];api.GetFileVersionInfoW.restype=w.BOOL
    api.VerQueryValueW.argtypes=[c.c_void_p,w.LPCWSTR,c.POINTER(c.c_void_p),c.POINTER(w.UINT)];api.VerQueryValueW.restype=w.BOOL
    ignored=w.DWORD();size=api.GetFileVersionInfoSizeW(str(path),c.byref(ignored))
    if not size:raise ValueError(f'Missing PE version metadata: {path}')
    data=c.create_string_buffer(size)
    if not api.GetFileVersionInfoW(str(path),0,size,data):raise OSError(c.get_last_error())
    value=c.c_void_p();length=w.UINT()
    if not api.VerQueryValueW(data,'\\StringFileInfo\\040904b0\\'+key,c.byref(value),c.byref(length)):raise ValueError(f'Missing {key}: {path}')
    return c.wstring_at(value.value,length.value).rstrip('\0')
