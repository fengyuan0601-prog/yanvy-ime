// SPDX-License-Identifier: BSD-3-Clause
// Developer-only real settings window. All writes use the explicit test folder.
#include "preferences_ui.hpp"
#include <shellapi.h>
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,PWSTR,int show){
  int argc=0;auto argv=CommandLineToArgvW(GetCommandLineW(),&argc);
  if(!argv||argc!=2){if(argv)LocalFree(argv);return 2;}
  const std::filesystem::path root=argv[1];LocalFree(argv);
  if(!root.is_absolute()||!std::filesystem::is_directory(root))return 2;
  return yanvy::PreferencesUI(instance,show,root);
}
