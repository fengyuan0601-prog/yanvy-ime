// SPDX-License-Identifier: BSD-3-Clause
// Explicit fresh test directory only. No production user data or registration.
#include "common.hpp"
#include <aclapi.h>
#include <fstream>
#include <iostream>

namespace {
bool NotElevated() {
  HANDLE token=nullptr;TOKEN_ELEVATION elevation{};DWORD size=0;
  if(!OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY,&token))return false;
  const bool ok=GetTokenInformation(token,TokenElevation,&elevation,sizeof(elevation),&size)!=FALSE;
  CloseHandle(token);return ok&&!elevation.TokenIsElevated;
}
bool DenyFileCreation(const std::filesystem::path& directory) {
  PSECURITY_DESCRIPTOR descriptor=nullptr;PACL acl=nullptr;BOOL present=FALSE,defaulted=FALSE;
  const auto sid=yanvy::UserSid();
  const auto text=L"D:P(D;;0x2;;;"+sid+L")(A;;FA;;;"+sid+L")";
  if(!ConvertStringSecurityDescriptorToSecurityDescriptorW(text.c_str(),SDDL_REVISION_1,&descriptor,nullptr))return false;
  GetSecurityDescriptorDacl(descriptor,&present,&acl,&defaulted);
  const auto result=SetNamedSecurityInfoW(const_cast<wchar_t*>(directory.c_str()),SE_FILE_OBJECT,
      DACL_SECURITY_INFORMATION|PROTECTED_DACL_SECURITY_INFORMATION,nullptr,nullptr,acl,nullptr);
  LocalFree(descriptor);return result==ERROR_SUCCESS;
}
}
int wmain(int argc,wchar_t** argv) {
  if(argc==4&&std::wstring(argv[1])==L"--child"){
    // Only synthetic pass evidence is saved, never account IDs or environment.
    if(!NotElevated()||yanvy::UserSid()!=argv[3])return 10;
    HANDLE file=CreateFileW(argv[2],GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file==INVALID_HANDLE_VALUE)return 11;
    const char text[]="same-user ordinary child PASS\n";DWORD written=0;
    const bool ok=WriteFile(file,text,sizeof(text)-1,&written,nullptr)&&written==sizeof(text)-1;
    CloseHandle(file);return ok?0:12;
  }
  if(argc!=2)return 1;
  const std::filesystem::path base=argv[1];
  if(!base.is_absolute()||base.lexically_normal()!=base||base==base.root_path()||std::filesystem::exists(base))return 2;
  std::filesystem::create_directories(base/L"runtime");
  const auto runtime=base/L"runtime",selected=base/L"selected";
  if(yanvy::InstalledDataRoot(runtime)!=runtime)return 3;
  if(!WritePrivateProfileStringW(L"paths",L"data_root",selected.c_str(),(runtime/L"installation.ini").c_str()))return 4;
  if(yanvy::InstalledDataRoot(runtime)!=runtime)return 5;
  std::filesystem::create_directories(selected/L"data");
  if(yanvy::InstalledDataRoot(runtime)!=selected)return 6;
  if(!yanvy::CanCreateUserData(base/L"normal"/L"future"))return 7;
  const auto restricted=base/L"directory-only";
  std::filesystem::create_directory(restricted);
  if(!DenyFileCreation(restricted))return 8;
  HANDLE denied=CreateFileW((restricted/L"probe.txt").c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
  if(denied!=INVALID_HANDLE_VALUE){CloseHandle(denied);return 9;}
  if(yanvy::CanCreateUserData(restricted)){
    std::cerr<<"FAIL data root accepts a directory that cannot create settings files\n";return 13;
  }
  if(!NotElevated()){
    std::cout<<"Installed data fallback and file-write ACL PASS\n"
             <<"SKIP ordinary-user launch: elevated runner may have no interactive Explorer token\n";
    return 0;
  }
  const auto proof=base/L"child.txt";
  wchar_t module[32768]{};if(!GetModuleFileNameW(nullptr,module,DWORD(std::size(module))))return 14;
  const std::filesystem::path executable=module;
  const auto command=L"\""+executable.wstring()+L"\" --child \""+proof.wstring()+L"\" \""+yanvy::UserSid()+L"\"";
  if(!yanvy::StartUserProcess(executable,command,executable.parent_path()))return 15;
  for(int attempt=0;attempt<100&&!std::filesystem::exists(proof);++attempt)Sleep(50);
  std::ifstream stream(proof);std::string text;std::getline(stream,text);
  if(text!="same-user ordinary child PASS")return 16;
  std::cout<<"Installed data fallback, file-write ACL and same-user ordinary launch PASS\n";
  return 0;
}
