// SPDX-License-Identifier: BSD-3-Clause
#include "preferences_ui.hpp"
#include <objbase.h>
#include <iostream>
int wmain(int argc,wchar_t** argv){
  if((argc!=2&&argc!=5)||FAILED(CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED)))return 1;
  int result=1;
  if(argc==2)result=yanvy::PreferencesFixture(argv[1]);
  else if(std::wstring(argv[1])==L"--apply-schema"&&wcslen(argv[4])==1&&argv[4][0]>=L'0'&&argv[4][0]<L'0'+ime::kSchemaCount)
    result=yanvy::ApplyInputScheme(argv[2],argv[3],uint32_t(argv[4][0]-L'0'));
  CoUninitialize();
  std::cout<<"preferences_fixture="<<result<<" (isolated files only)\n";return result;
}
