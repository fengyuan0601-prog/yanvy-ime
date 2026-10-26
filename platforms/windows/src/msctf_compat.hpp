// SPDX-License-Identifier: BSD-3-Clause
// Missing public COM ABI declarations in MinGW-w64's msctf.h.
// Verified against Microsoft's msctf.h and documented TSF interfaces:
// https://github.com/microsoft/win32metadata/blob/main/generation/WinSDK/RecompiledIdlHeaders/um/msctf.h
#pragma once
#include <msctf.h>
#ifndef __ITfTextInputProcessorEx_INTERFACE_DEFINED__
#define __ITfTextInputProcessorEx_INTERFACE_DEFINED__
inline constexpr IID IID_ITfTextInputProcessorEx{0x6e4e2102,0xf9cd,0x433d,{0xb4,0x96,0x30,0x3c,0xe0,0x3a,0x65,0x07}};
struct ITfTextInputProcessorEx : ITfTextInputProcessor {
  virtual HRESULT STDMETHODCALLTYPE ActivateEx(ITfThreadMgr*,TfClientId,DWORD) = 0;
};
#endif
#ifndef __ITfTextLayoutSink_INTERFACE_DEFINED__
#define __ITfTextLayoutSink_INTERFACE_DEFINED__
inline constexpr IID IID_ITfTextLayoutSink{0x2af2d06a,0xdd5b,0x4927,{0xa0,0xb4,0x54,0xf1,0x9c,0x91,0xfa,0xde}};
enum TfLayoutCode { TF_LC_CREATE=0, TF_LC_CHANGE=1, TF_LC_DESTROY=2 };
struct ITfTextLayoutSink : IUnknown {
  virtual HRESULT STDMETHODCALLTYPE OnLayoutChange(ITfContext*,TfLayoutCode,ITfContextView*) = 0;
};
#endif
#ifndef TF_CLIENTID_NULL
#define TF_CLIENTID_NULL ((TfClientId)0)
#endif
#ifndef TF_IPPMF_FORSESSION
#define TF_IPPMF_FORSESSION 0x20000000
#endif
