// SPDX-License-Identifier: BSD-3-Clause
#pragma once
#include <windows.h>
#include <filesystem>
#include <string>

namespace yanvy {
// A live handle is the transaction marker. No wait or mutex ownership is used,
// so a second UI on the same thread or in another session cannot join it.
class InstallerTransaction {
  HANDLE handle_=nullptr;
 public:
  InstallerTransaction()=default;
  InstallerTransaction(const InstallerTransaction&)=delete;
  InstallerTransaction& operator=(const InstallerTransaction&)=delete;
  ~InstallerTransaction(){Release();}
  DWORD Acquire(const std::wstring& name){
    if(handle_)return ERROR_BUSY;
    HANDLE candidate=CreateMutexW(nullptr,FALSE,name.c_str());const DWORD error=GetLastError();
    if(!candidate)return error==ERROR_ACCESS_DENIED?ERROR_BUSY:error;
    if(error==ERROR_ALREADY_EXISTS){CloseHandle(candidate);return ERROR_BUSY;}
    handle_=candidate;return ERROR_SUCCESS;
  }
  void Release(){if(handle_){CloseHandle(handle_);handle_=nullptr;}}
};
inline bool InstallationReferencesData(const std::filesystem::path& target,const std::filesystem::path& data){
  return !data.empty()&&_wcsicmp(target.c_str(),data.c_str())==0;
}
}
