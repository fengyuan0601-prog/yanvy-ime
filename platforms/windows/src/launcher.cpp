// SPDX-License-Identifier: BSD-3-Clause
// Unelevated public UI; explicit runas worker; protected extraction.
#include "installer_ui.hpp"
#include <shellapi.h>
#include <sddl.h>
#include <objbase.h>
#include <shlobj.h>

namespace {
#ifndef YANVY_PREVIEW_ONLY
int Worker(HINSTANCE instance,bool uninstall,const std::filesystem::path& target,uint32_t schema){
  if(!IsUserAnAdmin())return ERROR_ELEVATION_REQUIRED;
  if(!yanvy::ValidInstallPath(target))return 10;
  if(schema>=ime::kSchemaCount)return 10;
  if(uninstall&&target!=yanvy::ModuleDir())return 10;
  HANDLE lock=CreateMutexW(nullptr,FALSE,L"Global\\YANVY.Setup.Transaction");
  DWORD lock_error=GetLastError();
  if(!lock||lock_error==ERROR_ALREADY_EXISTS){if(lock)CloseHandle(lock);return ERROR_BUSY;}
  struct Release { HANDLE handle;~Release(){CloseHandle(handle);} } release{lock};
  bool prefer_e=GetDriveTypeW(L"E:\\")==DRIVE_FIXED;std::filesystem::path fallback;
  if(!prefer_e){
    wchar_t buffer[32768]{};DWORD length=GetTempPathW(DWORD(std::size(buffer)),buffer);
    if(!length||length>=std::size(buffer))return 40;
    fallback=std::filesystem::path(buffer);
  }
  auto temporary=yanvy::SelectSetupTemp(prefer_e,fallback);
  if(!yanvy::ValidInstallPath(temporary/L"guard"))return 10;
  std::error_code error;std::filesystem::create_directories(temporary,error);
  if(error||!SetEnvironmentVariableW(L"TEMP",temporary.c_str())||!SetEnvironmentVariableW(L"TMP",temporary.c_str())||
      !SetEnvironmentVariableW(L"YANVY_SETUP_TEMP_READY",L"1"))return 40;
  auto schema_text=std::to_wstring(schema);if(!SetEnvironmentVariableW(L"YANVY_SETUP_SCHEMA",schema_text.c_str()))return 40;
  std::filesystem::path payload,transaction;
  if(uninstall){payload=target/L"Uninstall-core.exe";if(!std::filesystem::is_regular_file(payload,error))return 40;}
  else{
    auto resource=FindResourceW(instance,MAKEINTRESOURCEW(101),RT_RCDATA);
    if(!resource)return 40;
    auto bytes=LockResource(LoadResource(instance,resource));DWORD size=SizeofResource(instance,resource);
    if(!bytes||!size)return 40;
    GUID unique{};wchar_t name[64]{};if(FAILED(CoCreateGuid(&unique)))return 40;StringFromGUID2(unique,name,64);
    transaction=temporary/(std::wstring(L"YANVY-Setup-")+name);
    // A new protected directory prevents unelevated replacement of the payload.
    PSECURITY_DESCRIPTOR descriptor=nullptr;
    if(!ConvertStringSecurityDescriptorToSecurityDescriptorW(L"D:P(A;;FA;;;SY)(A;;FA;;;BA)",SDDL_REVISION_1,&descriptor,nullptr))return 40;
    SECURITY_ATTRIBUTES security{sizeof(security),descriptor,FALSE};BOOL created=CreateDirectoryW(transaction.c_str(),&security);LocalFree(descriptor);
    if(!created)return 40;
    payload=transaction/L"payload.exe";HANDLE output=CreateFileW(payload.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_TEMPORARY,nullptr);
    if(output==INVALID_HANDLE_VALUE){RemoveDirectoryW(transaction.c_str());return 40;}
    DWORD written=0;bool ok=WriteFile(output,bytes,size,&written,nullptr)&&written==size;CloseHandle(output);
    if(!ok){DeleteFileW(payload.c_str());RemoveDirectoryW(transaction.c_str());return 40;}
  }
  // NSIS /D must be last and unquoted, even when the path contains spaces.
  std::wstring command=L"\""+payload.wstring()+L"\" /S";
  if(!uninstall)command+=L" /D="+target.wstring();
  else command+=L" _?="+target.wstring(); // Wait for actual cleanup, not the relocating stub.
  STARTUPINFOW start{sizeof(start)};PROCESS_INFORMATION process{};DWORD code=40;
  if(CreateProcessW(payload.c_str(),command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,temporary.c_str(),&start,&process)){
    CloseHandle(process.hThread);WaitForSingleObject(process.hProcess,INFINITE);GetExitCodeProcess(process.hProcess,&code);CloseHandle(process.hProcess);
  }
  if(!uninstall){DeleteFileW(payload.c_str());RemoveDirectoryW(transaction.c_str());}
  return int(code);
}
#endif
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,PWSTR,int show){
  HRESULT initialized=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);if(FAILED(initialized))return 1;
  int count=0;auto args=CommandLineToArgvW(GetCommandLineW(),&count);int result=1;
  if(args){
    const std::wstring option=count>1?args[1]:L"";
    if(option==L"--render-preview"&&count==5)result=yanvy::RenderInstaller(instance,args[2],UINT(_wtoi(args[3])),_wtoi(args[4]));
    else if(option==L"--check-path"&&count==3)result=yanvy::ValidInstallPath(args[2])?0:10;
    else if(option==L"--check-fallback"&&count==2){
      auto home=std::filesystem::path(L"C:\\Users\\Example\\AppData\\Local");
      auto temp=home/L"Temp";
      result=(yanvy::SelectDefaultInstall(false,home)==home/L"Programs"/L"YANVY"/yanvy::kVersion&&
          yanvy::SelectSetupTemp(false,temp)==temp&&yanvy::SelectSetupTemp(true,temp)==std::filesystem::path(L"E:\\Codex\\Temp")&&
          yanvy::ResolveInstallPath(std::filesystem::path(L"D:\\YANVY")/yanvy::kVersion)==std::filesystem::path(L"D:\\YANVY")/yanvy::kVersion&&
          yanvy::ResolveInstallPath(L"D:\\YANVY\\0.1.0-rc.4")==std::filesystem::path(L"D:\\YANVY")/yanvy::kVersion&&
          yanvy::ResolveInstallPath(L"D:\\YANVY")==std::filesystem::path(L"D:\\YANVY")/yanvy::kVersion&&
          yanvy::ResolveInstallPath(L"D:\\apps")==std::filesystem::path(L"D:\\apps\\YANVY")/yanvy::kVersion)?0:1;
    }
#ifndef YANVY_PREVIEW_ONLY
    else if(option==L"--worker-install"&&count==4&&wcslen(args[3])==1&&args[3][0]>=L'0'&&args[3][0]<L'0'+ime::kSchemaCount)result=Worker(instance,false,args[2],uint32_t(args[3][0]-L'0'));
    else if(option==L"--worker-uninstall"&&count==3)result=Worker(instance,true,args[2],1);
#endif
    else if(option.empty()||option==L"--preview"){
#ifdef YANVY_PREVIEW_ONLY
      result=yanvy::InstallerUI(instance,false,true,show);
#else
      bool installer=FindResourceW(instance,MAKEINTRESOURCEW(101),RT_RCDATA)!=nullptr;
      result=yanvy::InstallerUI(instance,!installer,option==L"--preview",show);
#endif
    }
    LocalFree(args);
  }
  CoUninitialize();return result;
}
