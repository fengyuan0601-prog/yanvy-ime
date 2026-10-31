// SPDX-License-Identifier: BSD-3-Clause
// Unelevated public UI; explicit runas worker; protected extraction.
#include "installer_ui.hpp"
#include <shellapi.h>
#include <sddl.h>
#include <objbase.h>
#include <shlobj.h>
#include "machine_store.hpp"
#include "package_integrity.hpp"
#include "installer_transaction.hpp"
#include "installer_startup.hpp"

namespace {
#ifndef YANVY_PREVIEW_ONLY
int Worker(HINSTANCE instance,bool uninstall,const std::filesystem::path& target,uint32_t schema){
  if(!IsUserAnAdmin())return ERROR_ELEVATION_REQUIRED;
  if(!yanvy::ValidInstallPath(target))return 10;
  if(schema>=ime::kSchemaCount)return 10;
  if(uninstall&&(target!=yanvy::ModuleDir()||FAILED(yanvy::ValidateMachineRuntime(target))))return 41;
  const auto machine=uninstall?target:yanvy::MachineRuntimeRoot(yanvy::kVersion);
  if(!uninstall&&FAILED(yanvy::PrepareMachineRuntime(machine)))return 41;
  HANDLE lock=CreateMutexW(nullptr,FALSE,L"Global\\YANVY.Setup.Transaction");
  DWORD lock_error=GetLastError();
  if(!lock||lock_error==ERROR_ALREADY_EXISTS){if(lock)CloseHandle(lock);return ERROR_BUSY;}
  struct Release { HANDLE handle;~Release(){CloseHandle(handle);} } release{lock};
  // This flag is set only by this worker after comparing the existing runtime
  // with the manifest embedded in THIS installer, never its BUILD-INFO.json.
  if(!SetEnvironmentVariableW(L"YANVY_REPAIR_VERIFIED",L"0"))return 40;
  if(!uninstall){
    yanvy::PackageManifest manifest;if(!yanvy::LoadPackageManifest(instance,manifest))return 40;
    const auto destination=yanvy::InspectPackageDestination(machine,manifest,false);
    if(destination==yanvy::PackageDestination::rejected)return yanvy::kPackageMismatch;
    if(destination==yanvy::PackageDestination::reusable){
      std::error_code inspect_error;
      const auto uninstaller=machine/L"Uninstall-core.exe";
      const bool present=std::filesystem::exists(uninstaller,inspect_error);
      if(inspect_error||(present&&(!std::filesystem::is_regular_file(uninstaller,inspect_error)||inspect_error))||!yanvy::PackagePathWithoutReparse(uninstaller))return yanvy::kPackageMismatch;
      if(!SetEnvironmentVariableW(L"YANVY_REPAIR_VERIFIED",L"1"))return 40;
    }
  }
  // Elevated executables and NSIS scratch files require protected ancestors,
  // not merely a protected child underneath a user-writable TEMP directory.
  GUID unique{};wchar_t name[40]{};
  if(FAILED(CoCreateGuid(&unique))||!StringFromGUID2(unique,name,40))return 40;
  auto temporary=yanvy::MachineRuntimeRoot(L"txn-"+std::wstring(name+1,36));
  if(FAILED(yanvy::PrepareMachineRuntime(temporary)))return 41;
  struct RemoveTemp{std::filesystem::path path;~RemoveTemp(){RemoveDirectoryW(path.c_str());}} remove_temp{temporary};
  std::error_code error;
  if(!SetEnvironmentVariableW(L"TEMP",temporary.c_str())||!SetEnvironmentVariableW(L"TMP",temporary.c_str())||
      !SetEnvironmentVariableW(L"YANVY_SETUP_TEMP_READY",L"1"))return 40;
  auto schema_text=std::to_wstring(schema);if(!SetEnvironmentVariableW(L"YANVY_SETUP_SCHEMA",schema_text.c_str()))return 40;
  if(!uninstall&&!SetEnvironmentVariableW(L"YANVY_DATA_LOCATION",target.c_str()))return 40;
  std::filesystem::path payload;
  if(uninstall){payload=target/L"Uninstall-core.exe";if(!std::filesystem::is_regular_file(payload,error))return 40;}
  else{
    auto resource=FindResourceW(instance,MAKEINTRESOURCEW(101),RT_RCDATA);
    if(!resource)return 40;
    auto bytes=LockResource(LoadResource(instance,resource));DWORD size=SizeofResource(instance,resource);
    if(!bytes||!size)return 40;
    payload=temporary/L"payload.exe";HANDLE output=CreateFileW(payload.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_TEMPORARY,nullptr);
    if(output==INVALID_HANDLE_VALUE)return 40;
    DWORD written=0;bool ok=WriteFile(output,bytes,size,&written,nullptr)&&written==size;CloseHandle(output);
    if(!ok){DeleteFileW(payload.c_str());return 40;}
  }
  // NSIS /D must be last and unquoted, even when the path contains spaces.
  std::wstring command=L"\""+payload.wstring()+L"\" /S";
  if(!uninstall)command+=L" /D="+machine.wstring();
  else command+=L" _?="+target.wstring(); // Wait for actual cleanup, not the relocating stub.
  STARTUPINFOW start{sizeof(start)};PROCESS_INFORMATION process{};DWORD code=40;
  if(CreateProcessW(payload.c_str(),command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,temporary.c_str(),&start,&process)){
    CloseHandle(process.hThread);WaitForSingleObject(process.hProcess,INFINITE);GetExitCodeProcess(process.hProcess,&code);CloseHandle(process.hProcess);
  }
  if(!uninstall)DeleteFileW(payload.c_str());
  return int(code);
}

// User-selected data paths are processed only with the original medium token.
// This payload contains dictionaries/documents, never an executable or DLL.
int DataWorker(HINSTANCE instance,const std::filesystem::path& target,bool cleanup){
  if(const DWORD permission=yanvy::InstallerUserPermission())return int(permission);
  if(!yanvy::ValidInstallPath(target))return 10;
  yanvy::InstallerTransaction cleanup_transaction;
  if(cleanup){
    // Even a directly invoked cleanup worker must not remove data committed by
    // another setup. Hold the machine transaction marker across check/delete.
    const DWORD locked=cleanup_transaction.Acquire(L"Global\\YANVY.Setup.Transaction");if(locked)return int(locked);
    wchar_t registered[32768]{};DWORD length=sizeof(registered);
    const LSTATUS read=RegGetValueW(HKEY_LOCAL_MACHINE,L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\YANVY-dev",L"InstallLocation",RRF_RT_REG_SZ|RRF_SUBKEY_WOW6464KEY,nullptr,registered,&length);
    if(read==ERROR_SUCCESS){
      const auto runtime=std::filesystem::path(registered);
      if(FAILED(yanvy::ValidateMachineRuntime(runtime)))return ERROR_ACCESS_DENIED;
      const auto metadata=runtime/L"installation.ini";const DWORD attributes=GetFileAttributesW(metadata.c_str());
      if(attributes==INVALID_FILE_ATTRIBUTES||(attributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT)))return ERROR_ACCESS_DENIED;
      wchar_t selected[32768]{};GetPrivateProfileStringW(L"paths",L"data_root",L"",selected,DWORD(std::size(selected)),metadata.c_str());
      const auto data=selected[0]?std::filesystem::path(selected):runtime;
      if(!yanvy::PlainLocalPath(data)||yanvy::InstallationReferencesData(target,data))return ERROR_BUSY;
    }else if(read!=ERROR_FILE_NOT_FOUND&&read!=ERROR_PATH_NOT_FOUND)return int(read);
  }
  if(!cleanup){
    yanvy::PackageManifest manifest;if(!yanvy::LoadPackageManifest(instance,manifest))return 40;
    const auto destination=yanvy::InspectPackageDestination(target,manifest,true);
    if(destination==yanvy::PackageDestination::rejected)return yanvy::kPackageMismatch;
    // No ownership of existing files is acquired: a later UAC cancellation or
    // registration failure must never invoke this attempt's data rollback.
    if(destination==yanvy::PackageDestination::reusable)return yanvy::kDataReused;
  }
  auto resource=FindResourceW(instance,MAKEINTRESOURCEW(103),RT_RCDATA);
  if(!resource)return 40;
  auto bytes=LockResource(LoadResource(instance,resource));DWORD size=SizeofResource(instance,resource);
  if(!bytes||!size)return 40;
  wchar_t temp[32768]{};DWORD n=GetTempPathW(DWORD(std::size(temp)),temp);
  if(!n||n>=std::size(temp))return 40;
  auto parent=yanvy::SelectSetupTemp(GetDriveTypeW(L"E:\\")==DRIVE_FIXED,std::filesystem::path(temp));
  std::error_code error;std::filesystem::create_directories(parent,error);
  if(error||!yanvy::ValidInstallPath(parent/L"guard"))return 40;
  GUID id{};wchar_t name[40]{};
  if(FAILED(CoCreateGuid(&id))||!StringFromGUID2(id,name,40))return 40;
  auto transaction=parent/(std::wstring(L"YANVY-Data-")+name);
  if(!CreateDirectoryW(transaction.c_str(),nullptr))return 40;
  auto payload=transaction/L"data.exe";
  HANDLE output=CreateFileW(payload.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_TEMPORARY,nullptr);
  if(output==INVALID_HANDLE_VALUE){RemoveDirectoryW(transaction.c_str());return 40;}
  DWORD written=0;bool ok=WriteFile(output,bytes,size,&written,nullptr)&&written==size;CloseHandle(output);
  DWORD code=40;
  if(ok){
    std::wstring command=L"\""+payload.wstring()+L"\" /S"+(cleanup?std::wstring(L" /CLEANUP"):std::wstring())+L" /D="+target.wstring();
    STARTUPINFOW start{sizeof(start)};PROCESS_INFORMATION process{};
    if(CreateProcessW(payload.c_str(),command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,transaction.c_str(),&start,&process)){
      CloseHandle(process.hThread);WaitForSingleObject(process.hProcess,INFINITE);GetExitCodeProcess(process.hProcess,&code);CloseHandle(process.hProcess);
    }
  }
  DeleteFileW(payload.c_str());RemoveDirectoryW(transaction.c_str());return int(code);
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
          yanvy::ResolveInstallPath(L"D:\\YANVY\\0.1.0")==std::filesystem::path(L"D:\\YANVY")/yanvy::kVersion&&
          yanvy::ResolveInstallPath(L"D:\\YANVY")==std::filesystem::path(L"D:\\YANVY")/yanvy::kVersion&&
          yanvy::ResolveInstallPath(L"D:\\apps")==std::filesystem::path(L"D:\\apps\\YANVY")/yanvy::kVersion)?0:1;
    }
#ifndef YANVY_PREVIEW_ONLY
    else if(option==L"--worker-prepare-data"&&count==3)result=DataWorker(instance,args[2],false);
    else if(option==L"--worker-cleanup-data"&&count==3)result=DataWorker(instance,args[2],true);
    else if(option==L"--worker-install"&&count==4&&wcslen(args[3])==1&&args[3][0]>=L'0'&&args[3][0]<L'0'+ime::kSchemaCount)result=Worker(instance,false,args[2],uint32_t(args[3][0]-L'0'));
    else if(option==L"--worker-uninstall"&&count==3)result=Worker(instance,true,args[2],1);
#endif
    else if((option.empty()&&count==1)||((option==L"--preview"||option==L"--desktop-ui")&&count==2)){
#ifdef YANVY_PREVIEW_ONLY
      result=yanvy::InstallerUI(instance,false,true,show);
#else
      bool installer=FindResourceW(instance,MAKEINTRESOURCEW(101),RT_RCDATA)!=nullptr;
      DWORD startup_error=0;
      if(option!=L"--preview"){
        startup_error=yanvy::InstallerUserPermission();
        if(startup_error==ERROR_ELEVATION_REQUIRED&&option!=L"--desktop-ui"){
          wchar_t file[32768]{};const DWORD length=GetModuleFileNameW(nullptr,file,DWORD(std::size(file)));
          const HRESULT opened=length&&length<std::size(file)?yanvy::OpenInstallerOnDesktop(file):E_FAIL;
          if(SUCCEEDED(opened)){LocalFree(args);CoUninitialize();return 0;}
        }
      }
      result=yanvy::InstallerUI(instance,!installer,option==L"--preview",show,startup_error);
#endif
    }
    LocalFree(args);
  }
  CoUninitialize();return result;
}
