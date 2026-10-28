// SPDX-License-Identifier: BSD-3-Clause
#pragma once
#include <windows.h>
namespace yanvy {
// New service identity allows an already-running host to instantiate this
// implementation even when it still holds the old service's COM instance.
inline constexpr GUID kService{0x9877e941,0x5f7a,0x4f0a,{0x89,0x73,0xaf,0x5a,0x66,0x68,0x1d,0x15}};
inline constexpr GUID kProfile{0x35790895,0x59a7,0x4048,{0x85,0x64,0x8c,0x72,0x7f,0xf8,0xbe,0x5a}};
inline constexpr GUID kLegacyService{0xf0f81899,0x01e7,0x4f10,{0x9c,0x09,0x9c,0x8c,0x36,0x67,0x41,0x84}};
inline constexpr GUID kLegacyProfile{0xa95f02d3,0x1161,0x4b4b,{0x82,0x9a,0xe4,0xc9,0x0f,0x97,0x3b,0x8c}};
#if defined(YANVY_MODE_FIXTURE) || defined(YANVY_TSF_HOST_FIXTURE)
// Assigned random command identities by the fixture before activating TSF.
// Never dispatch SimulatePreservedKey to an installed product's command GUID.
inline GUID kToggleEnglish{},kToggleEnglishRelease{};
#else
inline constexpr GUID kToggleEnglish{0xe87969e6,0xb430,0x4a23,{0x86,0x29,0xf2,0x3e,0x2d,0x4a,0xae,0x35}};
inline constexpr GUID kToggleEnglishRelease{0xfbd0de3b,0x4fbc,0x4cce,{0x93,0x33,0x81,0x4e,0xd8,0xdb,0x67,0x83}};
#endif
inline constexpr wchar_t kClsid[] = L"{9877E941-5F7A-4F0A-8973-AF5A66681D15}";
inline constexpr wchar_t kLegacyClsid[] = L"{F0F81899-01E7-4F10-9C09-9C8C36674184}";
inline constexpr wchar_t kName[] = L"言舟 · YANVY";
inline constexpr LANGID kLanguage = 0x0804;
}
