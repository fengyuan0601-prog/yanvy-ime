// SPDX-License-Identifier: BSD-3-Clause
#include "preferences_ui.hpp"
#include "settings_compat.hpp"
#include <shellapi.h>
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,PWSTR,int show){
  int argc=0;auto argv=CommandLineToArgvW(GetCommandLineW(),&argc);
  if(!argv||argc==1){
    const HRESULT forwarded=yanvy::ForwardSupersededSettings(yanvy::ModuleDir());
    if(forwarded!=S_FALSE){if(argv)LocalFree(argv);return SUCCEEDED(forwarded)?0:1;}
  }
  if(argv&&argc==3&&std::wstring(argv[1])==L"--apply-schema"&&wcslen(argv[2])==1&&argv[2][0]>=L'0'&&argv[2][0]<L'0'+ime::kSchemaCount){
    uint32_t schema=uint32_t(argv[2][0]-L'0');LocalFree(argv);
    HRESULT initialized=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);if(FAILED(initialized))return 1;
    int result=yanvy::ApplyInputScheme(yanvy::ModuleDir(),yanvy::UserDataRoot(),schema);CoUninitialize();return result;
  }
  if(argv)LocalFree(argv);
  return yanvy::PreferencesUI(instance,show);
}
