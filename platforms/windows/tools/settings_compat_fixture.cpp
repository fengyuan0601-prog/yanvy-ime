// SPDX-License-Identifier: BSD-3-Clause
// Real fixed-file restoration in a private fake Program Files. No production
// registry, restart queue, settings windows, or installed component is touched.
#define YANVY_MACHINE_STORE_FIXTURE
#define YANVY_SETTINGS_COMPAT_FIXTURE
#include "settings_compat.hpp"
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
unsigned checks=0;
void Check(bool value,const char* label){++checks;if(!value)throw std::runtime_error(label);}
void Write(const std::filesystem::path& path,const std::string& bytes){std::ofstream out(path,std::ios::binary);out<<bytes;Check(bool(out),"write isolated fixture file");}
std::string Read(const std::filesystem::path& path){std::ifstream in(path,std::ios::binary);return {std::istreambuf_iterator<char>(in),{}};}
HRESULT Dacl(const std::filesystem::path& path,const std::wstring& extra=L""){
  const auto text=L"D:P(A;OICI;FA;;;"+yanvy::UserSid()+L")(A;OICI;GRGX;;;BU)"+extra;
  PSECURITY_DESCRIPTOR sd=nullptr;if(!ConvertStringSecurityDescriptorToSecurityDescriptorW(text.c_str(),SDDL_REVISION_1,&sd,nullptr))return HRESULT_FROM_WIN32(GetLastError());
  BOOL present=FALSE,defaulted=FALSE;PACL acl=nullptr;GetSecurityDescriptorDacl(sd,&present,&acl,&defaulted);
  const DWORD result=SetNamedSecurityInfoW(const_cast<wchar_t*>(path.c_str()),SE_FILE_OBJECT,DACL_SECURITY_INFORMATION|PROTECTED_DACL_SECURITY_INFORMATION,nullptr,nullptr,acl,nullptr);
  LocalFree(sd);return HRESULT_FROM_WIN32(result);
}
}
int wmain(int argc,wchar_t** argv){
  try{
    if(argc==1){
      const auto own=yanvy::ModuleDir(),store=own.parent_path();
      if(Read(store.parent_path()/L"compat-fixture.marker")!="YANVY isolated compatibility fixture")return 2;
      yanvy::machine_store_fixture_root=store;yanvy::machine_store_fixture_owner_sid=yanvy::UserSid();
      yanvy::settings_compat_fixture_registered_runtime=store/L"0.1.4";
      const HRESULT hr=yanvy::ForwardSupersededSettings(own);if(hr!=S_FALSE)return SUCCEEDED(hr)?0:3;
      Write(own/L"forwarded-launch.txt","current settings reached");return 0;
    }
    if(argc!=2)return 1;
    const std::filesystem::path base=argv[1];Check(base.is_absolute()&&base.lexically_normal()==base&&!std::filesystem::exists(base),"fresh explicit evidence directory");
    std::filesystem::create_directories(base/L"Program Files");
    yanvy::machine_store_fixture_root=base/L"Program Files"/L"YANVY";
    yanvy::machine_store_fixture_owner_sid=yanvy::UserSid();
    Check(SUCCEEDED(Dacl(base/L"Program Files")),"protect fake Program Files");
    Write(base/L"Program Files"/L"compat-fixture.marker","YANVY isolated compatibility fixture");
    const auto current=yanvy::MachineRuntimeRoot(L"0.1.4"),old=yanvy::MachineRuntimeRoot(L"0.1.2"),existing=yanvy::MachineRuntimeRoot(L"0.1.1");
    for(const auto& root:{current,old,existing})Check(SUCCEEDED(yanvy::PrepareMachineRuntime(root)),"create private protected version root");
    wchar_t self[32768]{};GetModuleFileNameW(nullptr,self,DWORD(std::size(self)));
    std::filesystem::copy_file(self,current/L"YANVY-Settings.exe");
    Write(old/L"YANVY-TSF.dll","retained old TSF bytes");Write(old/L"personal.txt","keep personal file");
    Write(existing/L"YANVY-TSF.dll","older TSF bytes");Write(existing/L"YANVY-Settings.exe","existing unknown settings bytes");
    HANDLE retained=CreateFileW((old/L"YANVY-TSF.dll").c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr);
    Check(retained!=INVALID_HANDLE_VALUE,"hold old DLL without write/delete sharing");
    std::vector<std::filesystem::path> scheduled;
    const auto schedule=[&](const std::filesystem::path& path){scheduled.push_back(path);return S_OK;};
    Check(yanvy::RestoreMissingSettingsEntries(current,schedule)==S_OK,"restore missing settings while old DLL remains loaded");
    Check(Read(old/L"YANVY-Settings.exe")==Read(current/L"YANVY-Settings.exe"),"restored entry uses exact trusted new bytes");
    Check(Read(old/L"YANVY-TSF.dll")=="retained old TSF bytes"&&Read(old/L"personal.txt")=="keep personal file","old DLL and personal files unchanged");
    Check(Read(existing/L"YANVY-Settings.exe")=="existing unknown settings bytes","existing file never overwritten");
    Check(scheduled.size()==1&&scheduled[0]==old/L"YANVY-Settings.exe","only newly restored fixed file scheduled for later deletion");
    Check(yanvy::RestoreMissingSettingsEntries(current,schedule)==S_OK&&scheduled.size()==1,"repeat restoration is idempotent");
    std::filesystem::path target;
    Check(yanvy::SettingsForwardTarget(old,current,target)==S_OK&&target==current/L"YANVY-Settings.exe","old entry resolves to current fixed settings");
    Check(yanvy::SettingsForwardTarget(current,current,target)==S_FALSE&&target.empty(),"current settings does not forward to itself");
    yanvy::SettingsToken context;Check(SUCCEEDED(yanvy::CurrentSettingsToken(context)),"inspect actual launch test token");
    if(yanvy::OrdinarySettingsToken(context)){
      DWORD launch_error=0;const auto entry=old/L"YANVY-Settings.exe";
      Check(yanvy::StartUserProcess(entry,L"\""+entry.wstring()+L"\"",old,&launch_error),"launch restored old fixed entry without a window");
      for(unsigned attempt=0;attempt<100&&!std::filesystem::exists(current/L"forwarded-launch.txt");++attempt)Sleep(10);
      Check(Read(current/L"forwarded-launch.txt")=="current settings reached"&&!std::filesystem::exists(old/L"forwarded-launch.txt"),"actual old entry forwards once to current executable");
    }else std::cout<<"SKIP actual forwarding process launch: medium desktop token required (file restoration and rejection checks continue)\n";
    CloseHandle(retained);
    const auto unknown=yanvy::MachineRuntimeRoot(L"9.9.9");Check(SUCCEEDED(yanvy::PrepareMachineRuntime(unknown)),"create unlisted version fixture");
    Write(unknown/L"YANVY-TSF.dll","unlisted version");
    Check(yanvy::RestoreMissingSettingsEntries(current,schedule)==S_OK&&!std::filesystem::exists(unknown/L"YANVY-Settings.exe"),"unlisted versions are not modified");
    Check(SUCCEEDED(Dacl(current/L"YANVY-Settings.exe",L"(A;;FW;;;WD)")),"inject writable source ACL");
    Check(FAILED(yanvy::RestoreMissingSettingsEntries(current,schedule))&&FAILED(yanvy::SettingsForwardTarget(old,current,target)),"untrusted source runtime is rejected");
    Check(SUCCEEDED(Dacl(current/L"YANVY-Settings.exe")),"restore private source protection");
    Check(DeleteFileW((old/L"YANVY-Settings.exe").c_str()),"remove own restored fixture file");
    Check(SUCCEEDED(Dacl(old,L"(A;OICI;FW;;;WD)")),"inject writable destination ACL");
    Check(FAILED(yanvy::RestoreMissingSettingsEntries(current,schedule))&&!std::filesystem::exists(old/L"YANVY-Settings.exe"),"writable previous directory is never repaired");
    Check(SUCCEEDED(Dacl(old)),"restore own destination protection");
    Check(yanvy::RestoreMissingSettingsEntries(current,[](const auto&){return E_ACCESSDENIED;})==E_ACCESSDENIED&&std::filesystem::exists(old/L"YANVY-Settings.exe"),"restart scheduling failure preserves working entry and reports warning");
    Check(DeleteFileW((current/L"YANVY-Settings.exe").c_str()),"remove own current fixture file");
    Check(yanvy::SettingsForwardTarget(old,current,target)==HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND),"missing current settings is an explicit failure");
    Check(Read(old/L"personal.txt")=="keep personal file","failure paths preserve personal file");
    std::cout<<"Settings compatibility restoration and forwarding PASS ("<<checks<<" checks; no production mutation)\n";return 0;
  }catch(const std::exception& error){std::cerr<<"FAIL "<<error.what()<<'\n';return 1;}
}
