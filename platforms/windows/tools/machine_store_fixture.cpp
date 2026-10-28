// SPDX-License-Identifier: BSD-3-Clause
// Explicit isolated root only; no machine registration, real install or UAC.
#define YANVY_MACHINE_STORE_FIXTURE
#include "machine_store.hpp"
#include "common.hpp"
#include <fstream>
#include <iostream>
#include <winioctl.h>

bool CreateFixtureJunction(const std::filesystem::path& link,const std::filesystem::path& target) {
  if(!CreateDirectoryW(link.c_str(),nullptr))return false;
  HANDLE directory=CreateFileW(link.c_str(),GENERIC_WRITE,0,nullptr,OPEN_EXISTING,
      FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
  if(directory==INVALID_HANDLE_VALUE)return false;
  struct Junction { DWORD tag; WORD length,reserved,substitute_offset,substitute_length,print_offset,print_length; wchar_t paths[32768]; } data{};
  const auto print=target.wstring(),substitute=L"\\??\\"+print;
  data.tag=IO_REPARSE_TAG_MOUNT_POINT;
  data.substitute_length=WORD(substitute.size()*sizeof(wchar_t));
  data.print_offset=WORD((substitute.size()+1)*sizeof(wchar_t));
  data.print_length=WORD(print.size()*sizeof(wchar_t));
  memcpy(data.paths,substitute.c_str(),(substitute.size()+1)*sizeof(wchar_t));
  memcpy(reinterpret_cast<BYTE*>(data.paths)+data.print_offset,print.c_str(),(print.size()+1)*sizeof(wchar_t));
  const DWORD bytes=DWORD(offsetof(Junction,paths)+(substitute.size()+print.size()+2)*sizeof(wchar_t));
  data.length=WORD(bytes-8);DWORD returned=0;
  const bool ok=DeviceIoControl(directory,FSCTL_SET_REPARSE_POINT,&data,bytes,nullptr,0,&returned,nullptr)!=FALSE;
  CloseHandle(directory);return ok;
}

HRESULT SetFixtureDacl(const std::filesystem::path& path,const std::wstring& rights) {
  PSECURITY_DESCRIPTOR sd=nullptr;
  const auto sddl=L"D:P(A;OICI;FA;;;"+yanvy::machine_store_fixture_owner_sid+L")"+rights;
  if(!ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(),SDDL_REVISION_1,&sd,nullptr))return HRESULT_FROM_WIN32(GetLastError());
  BOOL present=FALSE,defaulted=FALSE;PACL acl=nullptr;
  GetSecurityDescriptorDacl(sd,&present,&acl,&defaulted);
  auto result=SetNamedSecurityInfoW(const_cast<wchar_t*>(path.c_str()),SE_FILE_OBJECT,
      DACL_SECURITY_INFORMATION|PROTECTED_DACL_SECURITY_INFORMATION,nullptr,nullptr,acl,nullptr);
  LocalFree(sd);return HRESULT_FROM_WIN32(result);
}
int wmain(int argc,wchar_t** argv) {
  if(argc!=2)return 1;
  if(std::wstring(argv[1])==L"--check-system-root"){
    const auto root=yanvy::MachineStoreRoot().parent_path();
    const HRESULT hr=yanvy::ValidateMachineAncestors(root);
    std::cout<<"read-only system ancestor check=0x"<<std::hex<<static_cast<unsigned long>(hr)<<'\n';
    return SUCCEEDED(hr)?0:1;
  }
  const auto supplied=std::filesystem::path(argv[1]);
  if(!supplied.is_absolute()||supplied.lexically_normal()!=supplied||supplied==supplied.root_path()||std::filesystem::exists(supplied))return 2;
  const auto base=supplied;
  std::filesystem::create_directories(base/L"Program Files");
  yanvy::machine_store_fixture_root=base/L"Program Files"/L"YANVY";
  yanvy::machine_store_fixture_owner_sid=yanvy::UserSid();
  if(FAILED(SetFixtureDacl(base/L"Program Files",L"(A;OICI;GRGX;;;BU)")))return 18;
  const auto runtime=yanvy::MachineRuntimeRoot(L"0.1.0-fixture");
  HRESULT hr=yanvy::PrepareMachineRuntime(runtime);
  if(FAILED(hr)){std::cerr<<"prepare=0x"<<std::hex<<static_cast<unsigned long>(hr)<<'\n';return 3;}
  const auto file=runtime/L"dummy.dll";
  std::ofstream(file)<<"nonexecutable fixture\n";
  if(FAILED(yanvy::ValidateMachineRuntime(runtime)))return 4;
  if(FAILED(yanvy::ValidatePreviousCleanup(runtime)))return 23;
  const auto historical=base/L"Program Files"/L"Historical x86"/L"YANVY"/L"0.1.0-rc.9";
  std::filesystem::create_directories(historical);std::ofstream(historical/L"old.txt")<<"not executable\n";
  if(FAILED(yanvy::ValidatePreviousCleanup(historical)))return 24;
  if(SUCCEEDED(yanvy::ValidateMachineRuntime(historical)))return 25;
  if(SUCCEEDED(yanvy::ValidatePreviousCleanup(historical.parent_path()/L"random")))return 26;
  if(SUCCEEDED(yanvy::ValidateMachineRuntime(base/L"other")))return 5;
  if(SUCCEEDED(yanvy::PrepareMachineRuntime(runtime/L".."/L"escaped")))return 6;
  // A writable runtime file, root, or delete-child permission in any
  // ancestor must be rejected, even when all other objects are protected.
  if(FAILED(SetFixtureDacl(file,L"(A;;FW;;;WD)")))return 7;
  if(SUCCEEDED(yanvy::ValidateMachineRuntime(runtime)))return 8;
  if(FAILED(SetFixtureDacl(file,L"(A;;FRFX;;;BU)")))return 9;
  if(FAILED(SetFixtureDacl(runtime,L"(A;OICI;FA;;;WD)")))return 10;
  if(SUCCEEDED(yanvy::PrepareMachineRuntime(runtime)))return 11;
  if(FAILED(SetFixtureDacl(runtime,L"(A;OICI;GRGX;;;BU)")))return 12;
  if(FAILED(SetFixtureDacl(base/L"Program Files",L"(A;;0x40;;;WD)")))return 13;
  if(SUCCEEDED(yanvy::ValidateMachineRuntime(runtime)))return 14;
  if(FAILED(SetFixtureDacl(base/L"Program Files",L"(A;;GRGX;;;BU)")))return 15;
  if(FAILED(yanvy::ValidateMachineRuntime(runtime)))return 16;
  if(yanvy::MachineRuntimeShape(runtime/L"child")||yanvy::MachineRuntimeShape(yanvy::MachineStoreRoot()/L"bad name"))return 17;
  const auto outside=base/L"outside",link=runtime/L"redirect";
  std::filesystem::create_directory(outside);std::ofstream(outside/L"keep.txt")<<"preserve\n";
  if(!CreateFixtureJunction(link,outside))return 19;
  if(SUCCEEDED(yanvy::ValidateMachineRuntime(runtime)))return 20;
  if(!RemoveDirectoryW(link.c_str())||!std::filesystem::exists(outside/L"keep.txt"))return 21;
  if(FAILED(yanvy::ValidateMachineRuntime(runtime)))return 22;
  std::cout<<"Machine store path, file ACL, existing-root ACL, ancestor DELETE_CHILD, junction checks PASS\n";
  // Evidence remains in the explicit fixture directory. Never recursively
  // remove caller-provided directories or reset production ACLs.
  return 0;
}
