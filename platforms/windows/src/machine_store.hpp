// SPDX-License-Identifier: BSD-3-Clause
#pragma once
#include <windows.h>
#include <aclapi.h>
#include <sddl.h>
#include <shlobj.h>
#include <filesystem>
#include <string>
#include <vector>

namespace yanvy {
// Test injection is compiled out of every distributable. It is never read
// from environment variables, registry values or command-line arguments.
#ifdef YANVY_MACHINE_STORE_FIXTURE
inline std::filesystem::path machine_store_fixture_root;
inline std::wstring machine_store_fixture_owner_sid;
#endif
inline std::filesystem::path MachineStoreRoot() {
#ifdef YANVY_MACHINE_STORE_FIXTURE
  if (!machine_store_fixture_root.empty()) return machine_store_fixture_root;
#endif
  PWSTR value=nullptr;
  if (FAILED(SHGetKnownFolderPath(FOLDERID_ProgramFilesX64,0,nullptr,&value))) return {};
  auto root=std::filesystem::path(value)/L"YANVY";CoTaskMemFree(value);return root;
}
inline std::filesystem::path MachineRuntimeRoot(const std::wstring& version) {
  const auto root=MachineStoreRoot();return root.empty()?root:root/version;
}
inline bool SameMachinePath(const std::filesystem::path& a,const std::filesystem::path& b) {
  return _wcsicmp(a.c_str(),b.c_str())==0;
}
inline bool MachineRuntimeShape(const std::filesystem::path& path) {
  const auto root=MachineStoreRoot();const auto leaf=path.filename().wstring();
  if(root.empty()||!path.is_absolute()||path.lexically_normal()!=path||leaf.empty()||leaf==L"."||leaf==L".."||
      !SameMachinePath(path.parent_path(),root))return false;
  for(auto c:leaf)if(!((c>=L'0'&&c<=L'9')||(c>=L'a'&&c<=L'z')||(c>=L'A'&&c<=L'Z')||c==L'.'||c==L'-'||c==L'_'))return false;
  return leaf.back()!=L'.';
}
inline bool TrustedMachineSid(PSID sid) {
  if(!sid||!IsValidSid(sid))return false;
  if(IsWellKnownSid(sid,WinLocalSystemSid)||IsWellKnownSid(sid,WinBuiltinAdministratorsSid))return true;
  PSID installer=nullptr;bool trusted=false;
  if(ConvertStringSidToSidW(L"S-1-5-80-956008885-3418522649-1831038044-1853292631-2271478464",&installer)){
    trusted=EqualSid(sid,installer)!=FALSE;LocalFree(installer);
  }
#ifdef YANVY_MACHINE_STORE_FIXTURE
  if(!trusted&&!machine_store_fixture_owner_sid.empty()&&ConvertStringSidToSidW(machine_store_fixture_owner_sid.c_str(),&installer)){
    trusted=EqualSid(sid,installer)!=FALSE;LocalFree(installer);
  }
#endif
  return trusted;
}
inline HRESULT ValidateMachineObject(const std::filesystem::path& path,bool ancestor=false) {
  HANDLE object=CreateFileW(path.c_str(),READ_CONTROL|FILE_READ_ATTRIBUTES,
      FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,
      FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
  if(object==INVALID_HANDLE_VALUE)return HRESULT_FROM_WIN32(GetLastError());
  struct Close{HANDLE h;~Close(){CloseHandle(h);}} close{object};
  BY_HANDLE_FILE_INFORMATION info{};
  if(!GetFileInformationByHandle(object,&info))return HRESULT_FROM_WIN32(GetLastError());
  if(info.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT)return E_ACCESSDENIED;
  PSID owner=nullptr;PACL acl=nullptr;PSECURITY_DESCRIPTOR descriptor=nullptr;
  DWORD result=GetSecurityInfo(object,SE_FILE_OBJECT,OWNER_SECURITY_INFORMATION|DACL_SECURITY_INFORMATION,
      &owner,nullptr,&acl,nullptr,&descriptor);
  if(result!=ERROR_SUCCESS)return HRESULT_FROM_WIN32(result);
  struct Free{HLOCAL p;~Free(){LocalFree(p);}} free{descriptor};
  if(!TrustedMachineSid(owner)||!acl)return E_ACCESSDENIED;
  GENERIC_MAPPING mapping{FILE_GENERIC_READ,FILE_GENERIC_WRITE,FILE_GENERIC_EXECUTE,FILE_ALL_ACCESS};
  DWORD mutation=DELETE|WRITE_DAC|WRITE_OWNER|FILE_WRITE_ATTRIBUTES|FILE_WRITE_EA|FILE_DELETE_CHILD;
  // Ancestors may allow creating unrelated children (e.g. a volume root).
  // Deleting/renaming this path through DELETE_CHILD is never permitted.
  if(!ancestor)mutation|=FILE_WRITE_DATA|FILE_APPEND_DATA;
  for(DWORD i=0;i<acl->AceCount;++i){
    void* value=nullptr;if(!GetAce(acl,i,&value))return HRESULT_FROM_WIN32(GetLastError());
    const auto header=static_cast<ACE_HEADER*>(value);
    if(header->AceFlags&INHERIT_ONLY_ACE)continue;
    if(header->AceType==ACCESS_DENIED_ACE_TYPE)continue;
    if(header->AceType!=ACCESS_ALLOWED_ACE_TYPE)return E_ACCESSDENIED;
    auto ace=static_cast<ACCESS_ALLOWED_ACE*>(value);DWORD mask=ace->Mask;MapGenericMask(&mask,&mapping);
    if((mask&mutation)&&!TrustedMachineSid(&ace->SidStart))return E_ACCESSDENIED;
  }
  return S_OK;
}
inline HRESULT ValidateMachineAncestors(const std::filesystem::path& path) {
  if(!path.is_absolute()||path.lexically_normal()!=path||path.root_name().wstring().size()!=2)return E_ACCESSDENIED;
  if(GetDriveTypeW(path.root_path().c_str())!=DRIVE_FIXED)return E_ACCESSDENIED;
  std::vector<std::filesystem::path> paths;
  for(auto current=path;!current.empty();current=current.parent_path()){
    paths.push_back(current);if(current==current.root_path())break;
#ifdef YANVY_MACHINE_STORE_FIXTURE
    // The injected fake Program Files is this isolated fixture's trust root;
    // it must not depend on or modify the developer's actual E: root ACL.
    if(!machine_store_fixture_root.empty()&&SameMachinePath(current,machine_store_fixture_root.parent_path()))break;
#endif
  }
  for(auto i=paths.rbegin();i!=paths.rend();++i){HRESULT hr=ValidateMachineObject(*i,true);if(FAILED(hr))return hr;}
  return S_OK;
}
inline HRESULT ValidateProtectedTree(const std::filesystem::path& path) {
  HRESULT hr=ValidateMachineObject(path);if(FAILED(hr))return hr;
  std::error_code error;
  if(!std::filesystem::is_directory(path,error)||error)return E_ACCESSDENIED;
  std::filesystem::recursive_directory_iterator it(path,std::filesystem::directory_options::none,error),end;
  if(error)return HRESULT_FROM_WIN32(ERROR_ACCESS_DENIED);
  while(it!=end){
    hr=ValidateMachineObject(it->path());if(FAILED(hr))return hr;
    it.increment(error);if(error)return HRESULT_FROM_WIN32(ERROR_ACCESS_DENIED);
  }
  return S_OK;
}
inline HRESULT ValidateMachineRuntime(const std::filesystem::path& path) {
  if(!MachineRuntimeShape(path))return E_ACCESSDENIED;
  HRESULT hr=ValidateMachineAncestors(MachineStoreRoot().parent_path());if(FAILED(hr))return hr;
  hr=ValidateMachineObject(MachineStoreRoot());if(FAILED(hr))return hr;
  return ValidateProtectedTree(path);
}
inline bool VersionDirectoryName(const std::wstring& name) {
  size_t index=0;
  for(int component=0;component<3;++component){
    const auto start=index;while(index<name.size()&&name[index]>=L'0'&&name[index]<=L'9')++index;
    if(index==start)return false;
    if(component<2&&(index==name.size()||name[index++]!=L'.'))return false;
  }
  if(index==name.size())return true;
  if(name[index++]!=L'-'||index==name.size())return false;
  for(;index<name.size();++index){const auto c=name[index];
    if(!((c>=L'0'&&c<=L'9')||(c>=L'a'&&c<=L'z')||(c>=L'A'&&c<=L'Z')||c==L'.'||c==L'-'))return false;
  }
  return name.back()!=L'.'&&name.back()!=L'-';
}
// Historical installations can live under Program Files (x86) or another
// administrator-protected volume. This grants cleanup only, never execution,
// COM registration or rollback trust for an arbitrary previous directory.
inline HRESULT ValidatePreviousCleanup(const std::filesystem::path& path) {
  if(!path.is_absolute()||path.lexically_normal()!=path||!VersionDirectoryName(path.filename().wstring())||
      !SameMachinePath(path.parent_path().filename(),L"YANVY"))return E_ACCESSDENIED;
  HRESULT hr=ValidateMachineAncestors(path.parent_path().parent_path());if(FAILED(hr))return hr;
  hr=ValidateMachineObject(path.parent_path());if(FAILED(hr))return hr;
  return ValidateProtectedTree(path);
}
inline HRESULT PrepareMachineRuntime(const std::filesystem::path& path) {
  if(!MachineRuntimeShape(path))return E_ACCESSDENIED;
  HRESULT hr=ValidateMachineAncestors(MachineStoreRoot().parent_path());if(FAILED(hr))return hr;
  std::wstring privileged=L"BA";
#ifdef YANVY_MACHINE_STORE_FIXTURE
  if(!machine_store_fixture_owner_sid.empty())privileged=machine_store_fixture_owner_sid;
#endif
  const auto sddl=L"O:"+privileged+L"G:BAD:P(A;OICI;FA;;;SY)(A;OICI;FA;;;BA)(A;OICI;FA;;;"+privileged+L")(A;OICI;GRGX;;;BU)";
  PSECURITY_DESCRIPTOR descriptor=nullptr;
  if(!ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(),SDDL_REVISION_1,&descriptor,nullptr))return HRESULT_FROM_WIN32(GetLastError());
  struct Free{HLOCAL p;~Free(){LocalFree(p);}} free{descriptor};
  SECURITY_ATTRIBUTES security{sizeof(security),descriptor,FALSE};
  for(const auto& directory:{MachineStoreRoot(),path}){
    if(!CreateDirectoryW(directory.c_str(),&security)&&GetLastError()!=ERROR_ALREADY_EXISTS)return HRESULT_FROM_WIN32(GetLastError());
    hr=ValidateMachineObject(directory);if(FAILED(hr))return hr;
  }
  return ValidateMachineRuntime(path);
}
}
