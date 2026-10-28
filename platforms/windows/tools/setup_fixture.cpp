// SPDX-License-Identifier: BSD-3-Clause
// Test-only transaction callbacks. No TSF, registry, broker or real input.
#include <windows.h>
#include <filesystem>
#include <fstream>
#include <string>
int wmain(int argc,wchar_t** argv){
  wchar_t executable[32768]{};GetModuleFileNameW(nullptr,executable,32768);
  auto root=std::filesystem::path(executable).parent_path();
  std::wstring option=argc>1?argv[1]:L"";std::string category;
  if(option==L"--check-dependencies"){
    auto probe=root/L"real-dependency-probe.exe";
    if(!std::filesystem::exists(probe))return 0;
    std::wstring command=L"\""+probe.wstring()+L"\" --check-dependencies";
    STARTUPINFOW startup{sizeof(startup)};PROCESS_INFORMATION process{};
    if(!CreateProcessW(probe.c_str(),command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,root.c_str(),&startup,&process))return 1;
    CloseHandle(process.hThread);WaitForSingleObject(process.hProcess,INFINITE);DWORD code=1;GetExitCodeProcess(process.hProcess,&code);CloseHandle(process.hProcess);return int(code);
  }
  if(option==L"--register")category="register";else if(option==L"--unregister")category="unregister";
  else if(option==L"--check")category="check";else if(option==L"--stop"||option==L"--stop-previous-brokers")category="stop";else return 1;
  std::ofstream(root/L"fixture-trace.txt",std::ios::app)<<category<<'\n';
  int code=0;std::ifstream(root/(category+"-result.txt"))>>code;return code;
}
