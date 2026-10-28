// SPDX-License-Identifier: BSD-3-Clause
#include "../src/installer_diagnostics.hpp"
#include <iostream>

int wmain() {
  using namespace yanvy;
  InstallerEnvironment env{true,10,0,26100,PROCESSOR_ARCHITECTURE_AMD64,PROCESSOR_ARCHITECTURE_INTEL};
  auto report=FormatInstallerDiagnostic(InstallerPhase::engine_check,0xC000001D,env);
  const std::wstring expected=std::wstring(L"YANVY diagnostic v1\r\nrelease=")+kVersion+L"\r\nwindows=10.0.26100\r\nnative_arch=x64\r\nprocess_arch=x86\r\nphase=engine-check\r\nresult=0xC000001D\r\ncategory=illegal-instruction\r\n";
  if(report!=expected)return 1;
  report=FormatInstallerDiagnostic(InstallerPhase::engine_check,26,env);
  if(report.find(L"original-exit-unavailable")==std::wstring::npos)return 2;
  report=FormatInstallerDiagnostic(InstallerPhase::engine_check,0xA1260001,env);
  if(report.find(L"result=0xA1260001\r\nchild_exit=0x00000001")==std::wstring::npos)return 3;
  report=FormatInstallerDiagnostic(InstallerPhase::unknown,0xC0000135,{});
  if(report.find(L"windows=unknown\r\nnative_arch=unknown\r\nprocess_arch=unknown\r\nphase=unknown")==std::wstring::npos ||
     report.find(L"dependency-not-found")==std::wstring::npos)return 4;
  auto actual=CollectInstallerEnvironment();
  if(actual.native_architecture==PROCESSOR_ARCHITECTURE_UNKNOWN || actual.process_architecture==PROCESSOR_ARCHITECTURE_UNKNOWN)return 5;
  // No collected machine details are written by this fixture.
  std::cout << "Minimal installer diagnostics and privacy whitelist PASS\n";
  return 0;
}
