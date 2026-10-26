// SPDX-License-Identifier: BSD-3-Clause
#include "preferences_ui.hpp"
#include <objbase.h>
#include <iostream>
int wmain(int argc,wchar_t** argv){
  if(argc!=2||FAILED(CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED)))return 1;
  int result=yanvy::PreferencesFixture(argv[1]);CoUninitialize();
  std::cout<<"preferences_fixture="<<result<<" (isolated files only)\n";return result;
}
