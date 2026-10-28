// SPDX-License-Identifier: BSD-3-Clause
#pragma once
#include <windows.h>
#include <cstring>
#include <string>
#include "version.hpp"

namespace yanvy {
// A closed set of phases avoids passing command lines, paths, or exception
// messages into user-shareable diagnostics. The worker supplies the phase;
// an exit status alone cannot reliably identify which operation failed.
enum class InstallerPhase {
  unknown, destination, extraction, dependencies, engine_launch, engine_check,
  registration, rollback, cleanup, uninstall
};
inline const wchar_t* DiagnosticPhaseName(InstallerPhase phase) {
  switch (phase) {
    case InstallerPhase::destination: return L"destination";
    case InstallerPhase::extraction: return L"extraction";
    case InstallerPhase::dependencies: return L"dependencies";
    case InstallerPhase::engine_launch: return L"engine-launch";
    case InstallerPhase::engine_check: return L"engine-check";
    case InstallerPhase::registration: return L"registration";
    case InstallerPhase::rollback: return L"rollback";
    case InstallerPhase::cleanup: return L"cleanup";
    case InstallerPhase::uninstall: return L"uninstall";
    default: return L"unknown";
  }
}
inline const wchar_t* DiagnosticArchitecture(WORD architecture) {
  switch (architecture) {
    case PROCESSOR_ARCHITECTURE_AMD64: return L"x64";
    case PROCESSOR_ARCHITECTURE_INTEL: return L"x86";
    case PROCESSOR_ARCHITECTURE_ARM64: return L"arm64";
    case PROCESSOR_ARCHITECTURE_ARM: return L"arm";
    default: return L"unknown";
  }
}
struct InstallerEnvironment {
  bool windows_version_known = false;
  DWORD major = 0, minor = 0, build = 0;
  WORD native_architecture = PROCESSOR_ARCHITECTURE_UNKNOWN;
  WORD process_architecture = PROCESSOR_ARCHITECTURE_UNKNOWN;
};
inline InstallerEnvironment CollectInstallerEnvironment() {
  InstallerEnvironment result;
  SYSTEM_INFO system{}; GetNativeSystemInfo(&system);
  result.native_architecture = system.wProcessorArchitecture;
#if defined(_M_X64) || defined(__x86_64__)
  result.process_architecture = PROCESSOR_ARCHITECTURE_AMD64;
#elif defined(_M_IX86) || defined(__i386__)
  result.process_architecture = PROCESSOR_ARCHITECTURE_INTEL;
#elif defined(_M_ARM64) || defined(__aarch64__)
  result.process_architecture = PROCESSOR_ARCHITECTURE_ARM64;
#endif
  // Avoid GetVersionEx's manifest-dependent version lie. If this system API
  // is unavailable, report unknown rather than inventing a Windows version.
  using ReadVersion = LONG (WINAPI*)(OSVERSIONINFOW*);
  HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
  FARPROC address = ntdll ? GetProcAddress(ntdll,"RtlGetVersion") : nullptr;
  ReadVersion read = nullptr;
  static_assert(sizeof(read)==sizeof(address));
  std::memcpy(&read,&address,sizeof(read));
  OSVERSIONINFOW version{}; version.dwOSVersionInfoSize = sizeof(version);
  if (read && read(&version) >= 0) {
    result.windows_version_known = true;
    result.major = version.dwMajorVersion; result.minor = version.dwMinorVersion;
    result.build = version.dwBuildNumber;
  }
  return result;
}
inline std::wstring DiagnosticHex(DWORD code) {
  constexpr wchar_t digits[] = L"0123456789ABCDEF";
  std::wstring text = L"0x00000000";
  for (unsigned i=0;i<8;++i) text[9-i] = digits[(code >> (i*4)) & 15];
  return text;
}
inline const wchar_t* DiagnosticExitCategory(DWORD code) {
  switch (code) {
    case 0: return L"success";
    case 0xC000001D: return L"illegal-instruction";
    case 0xC0000005: return L"access-violation";
    case 0xC000007B: return L"invalid-image-format";
    case 0xC0000135: return L"dependency-not-found";
    case 0xC0000142: return L"dll-initialization-failed";
    case 0xC0000409: return L"fail-fast";
    default: return L"unclassified";
  }
}
inline std::wstring FormatInstallerDiagnostic(InstallerPhase phase, DWORD result,
    const InstallerEnvironment& environment) {
  // All strings come from compiled constants; the API accepts no paths,
  // messages, user text or other arbitrary strings.
  std::wstring text = L"YANVY diagnostic v1\r\nrelease=";
  text += kVersion;
  text += L"\r\nwindows=";
  text += environment.windows_version_known ? std::to_wstring(environment.major)+L"."+
      std::to_wstring(environment.minor)+L"."+std::to_wstring(environment.build) : L"unknown";
  text += L"\r\nnative_arch="; text += DiagnosticArchitecture(environment.native_architecture);
  text += L"\r\nprocess_arch="; text += DiagnosticArchitecture(environment.process_architecture);
  text += L"\r\nphase="; text += DiagnosticPhaseName(phase);
  text += L"\r\nresult=" + DiagnosticHex(result);
  // Existing installers wrap unexpected small engine exits to distinguish
  // them from their own result codes. Keep both values, not just the low word.
  const bool wrapped = (result & 0xFFFF0000u) == 0xA1260000u;
  const DWORD child = wrapped ? result & 0xFFFFu : result;
  if (wrapped) text += L"\r\nchild_exit=" + DiagnosticHex(child);
  text += L"\r\ncategory="; text += DiagnosticExitCategory(child);
  if (phase == InstallerPhase::engine_check && result == 26)
    text += L"\r\nlegacy_detail=original-exit-unavailable";
  text += L"\r\n";
  return text;
}
}  // namespace yanvy
