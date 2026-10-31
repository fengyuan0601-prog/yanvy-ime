// SPDX-License-Identifier: BSD-3-Clause
#pragma once
#include "common.hpp"
#include "machine_store.hpp"

namespace yanvy {
#ifdef YANVY_SETTINGS_COMPAT_FIXTURE
inline std::filesystem::path settings_compat_fixture_registered_runtime;
#endif
inline HRESULT RegisteredSettingsRuntime(std::filesystem::path& runtime){
#ifdef YANVY_SETTINGS_COMPAT_FIXTURE
  runtime=settings_compat_fixture_registered_runtime;
#else
  runtime.clear();wchar_t value[32768]{};DWORD bytes=sizeof(value);
  const LSTATUS read=RegGetValueW(HKEY_LOCAL_MACHINE,
      L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\YANVY-dev",L"InstallLocation",
      RRF_RT_REG_SZ|RRF_SUBKEY_WOW6464KEY,nullptr,value,&bytes);
  if(read!=ERROR_SUCCESS)return HRESULT_FROM_WIN32(read);
  runtime=value;
#endif
  if(!VersionDirectoryName(runtime.filename().wstring()))return E_ACCESSDENIED;
  return ValidateMachineRuntime(runtime);
}
inline HRESULT SettingsForwardTarget(const std::filesystem::path& own,const std::filesystem::path& current,
    std::filesystem::path& executable){
  executable.clear();
  if(!VersionDirectoryName(own.filename().wstring())||FAILED(ValidateMachineRuntime(own))||
      !VersionDirectoryName(current.filename().wstring())||FAILED(ValidateMachineRuntime(current)))return E_ACCESSDENIED;
  if(SameMachinePath(own,current))return S_FALSE;
  const auto target=current/L"YANVY-Settings.exe";std::error_code error;
  if(!std::filesystem::is_regular_file(target,error)||error)return HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
  executable=target;return S_OK;
}
inline HRESULT ForwardSupersededSettings(const std::filesystem::path& own){
  // Development previews are not installed compatibility entry points.
  if(!MachineRuntimeShape(own)||!VersionDirectoryName(own.filename().wstring()))return S_FALSE;
  HRESULT hr=ValidateMachineRuntime(own);if(FAILED(hr))return hr;
  std::filesystem::path current,target;hr=RegisteredSettingsRuntime(current);if(FAILED(hr))return hr;
  hr=SettingsForwardTarget(own,current,target);if(hr!=S_OK)return hr;
  DWORD error=0;
  return StartUserProcess(target,L"\""+target.wstring()+L"\"",current,&error)?S_OK:HRESULT_FROM_WIN32(error);
}
inline constexpr const wchar_t* kSettingsCompatibilityVersions[]={L"0.1.0",L"0.1.1",L"0.1.2",L"0.1.3",L"0.1.4",L"0.1.5"};
template<class ScheduleRemoval>
HRESULT RestoreMissingSettingsEntries(const std::filesystem::path& current,ScheduleRemoval schedule){
  HRESULT hr=ValidateMachineRuntime(current);if(FAILED(hr))return hr;
  const auto source=current/L"YANVY-Settings.exe";std::error_code error;
  if(!std::filesystem::is_regular_file(source,error)||error)return HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
  HRESULT result=S_OK;
  for(const auto version:kSettingsCompatibilityVersions){
    const auto previous=MachineRuntimeRoot(version);if(SameMachinePath(previous,current))continue;
    const DWORD attributes=GetFileAttributesW((previous/L"YANVY-TSF.dll").c_str());
    if(attributes==INVALID_FILE_ATTRIBUTES){const DWORD read=GetLastError();if(read!=ERROR_FILE_NOT_FOUND&&read!=ERROR_PATH_NOT_FOUND&&SUCCEEDED(result))result=HRESULT_FROM_WIN32(read);continue;}
    if(attributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT)){if(SUCCEEDED(result))result=E_ACCESSDENIED;continue;}
    hr=ValidateMachineRuntime(previous);if(FAILED(hr)){if(SUCCEEDED(result))result=hr;continue;}
    const auto target=previous/L"YANVY-Settings.exe";
    // Never overwrite an existing executable or any unknown file. Only the
    // missing fixed entry used by a retained old TSF DLL is restored.
    if(GetFileAttributesW(target.c_str())!=INVALID_FILE_ATTRIBUTES)continue;
    const DWORD missing=GetLastError();
    if(missing!=ERROR_FILE_NOT_FOUND&&missing!=ERROR_PATH_NOT_FOUND){if(SUCCEEDED(result))result=HRESULT_FROM_WIN32(missing);continue;}
    if(!CopyFileW(source.c_str(),target.c_str(),TRUE)){if(SUCCEEDED(result))result=HRESULT_FROM_WIN32(GetLastError());continue;}
    hr=ValidateMachineObject(target);
    if(SUCCEEDED(hr))hr=schedule(target);
    // A scheduling failure preserves this valid compatibility entry and is
    // reported as a cleanup warning; it must not roll back the new install.
    if(FAILED(hr)&&SUCCEEDED(result))result=hr;
  }
  return result;
}
inline HRESULT RestoreMissingSettingsEntries(const std::filesystem::path& current){
  return RestoreMissingSettingsEntries(current,[](const std::filesystem::path& target){
    return MoveFileExW(target.c_str(),nullptr,MOVEFILE_DELAY_UNTIL_REBOOT)?S_OK:HRESULT_FROM_WIN32(GetLastError());
  });
}
}
