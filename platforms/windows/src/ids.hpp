// SPDX-License-Identifier: BSD-3-Clause
#pragma once
#include <windows.h>
namespace yanvy {
// New service identity allows an already-running host to instantiate this
// implementation even when it still holds the old service's COM instance.
inline constexpr GUID kService{0xe14b5f1f,0xb302,0x4d99,{0xb9,0x57,0x6e,0x95,0x88,0xac,0xb7,0xcc}};
inline constexpr GUID kProfile{0xbfc6e7bf,0x7e14,0x4696,{0x9a,0xca,0xec,0x5f,0xa8,0x7e,0xf2,0x58}};
// 0.1.0/0.1.1 hosts can still hold this class without the native mode button.
inline constexpr GUID kPreviousService{0x9877e941,0x5f7a,0x4f0a,{0x89,0x73,0xaf,0x5a,0x66,0x68,0x1d,0x15}};
inline constexpr GUID kPreviousProfile{0x35790895,0x59a7,0x4048,{0x85,0x64,0x8c,0x72,0x7f,0xf8,0xbe,0x5a}};
inline constexpr GUID kLegacyService{0xf0f81899,0x01e7,0x4f10,{0x9c,0x09,0x9c,0x8c,0x36,0x67,0x41,0x84}};
inline constexpr GUID kLegacyProfile{0xa95f02d3,0x1161,0x4b4b,{0x82,0x9a,0xe4,0xc9,0x0f,0x97,0x3b,0x8c}};
#if defined(YANVY_MODE_FIXTURE) || defined(YANVY_TSF_HOST_FIXTURE)
// Assigned random command identities by the fixture before activating TSF.
// Never dispatch SimulatePreservedKey to an installed product's command GUID.
inline GUID kToggleEnglish{},kToggleEnglishRelease{};
#else
inline constexpr GUID kToggleEnglish{0xda8329b7,0x5398,0x427c,{0x8b,0x13,0x01,0x54,0x4c,0x08,0x1f,0x11}};
inline constexpr GUID kToggleEnglishRelease{0x94d597b7,0xb1f4,0x4cab,{0x9c,0xb2,0x97,0xc9,0x6f,0x74,0x3b,0xd8}};
#endif
inline constexpr wchar_t kClsid[] = L"{E14B5F1F-B302-4D99-B957-6E9588ACB7CC}";
inline constexpr wchar_t kPreviousClsid[] = L"{9877E941-5F7A-4F0A-8973-AF5A66681D15}";
inline constexpr wchar_t kLegacyClsid[] = L"{F0F81899-01E7-4F10-9C09-9C8C36674184}";
struct PreviousTextService { const GUID* service;const GUID* profile;const wchar_t* clsid; };
inline constexpr PreviousTextService kPreviousTextServices[] = {
  {&kPreviousService,&kPreviousProfile,kPreviousClsid},
  {&kLegacyService,&kLegacyProfile,kLegacyClsid}
};
// GUID_TFCAT_TIPCAP_SYSTRAYSUPPORT (Windows SDK msctf.h). This capability is
// limited to the native TSF system-tray mode button; no immersive/UI-less claim.
inline constexpr GUID kCategorySystraySupport{0x25504fb4,0x7bab,0x4bc1,{0x9c,0x69,0xcf,0x81,0x89,0x0f,0x0e,0xf5}};
inline constexpr wchar_t kName[] = L"言舟 · YANVY";
inline constexpr LANGID kLanguage = 0x0804;
}
