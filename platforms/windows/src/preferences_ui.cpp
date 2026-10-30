// SPDX-License-Identifier: BSD-3-Clause
#include "preferences_ui.hpp"
#include "candidate_view.hpp"
#include "ui_controls.hpp"
#include <commctrl.h>
#include <shellapi.h>
#include <objbase.h>

namespace yanvy {
namespace {
enum {NavInput=101,NavAppearance,NavAbout,SaveButton,ResetButton,HelpButton,FontBox=112,ThemeBox};
struct Preferences {
  Appearance display;uint32_t schema=1,pinyin=1,double_schema=2;
  bool Equals(const Preferences& other)const{return display.font_size==other.display.font_size&&display.theme==other.display.theme&&schema==other.schema&&pinyin==other.pinyin&&double_schema==other.double_schema;}
};
bool SavePreferences(const std::filesystem::path& ini,const Preferences& value,bool apply_input=false){
  GUID guid{};wchar_t name[64]{};if(FAILED(CoCreateGuid(&guid)))return false;StringFromGUID2(guid,name,64);
  auto temporary=ini.parent_path()/(std::wstring(L"settings-pending-")+name+L".ini");
  if(!CopyFileW(ini.c_str(),temporary.c_str(),TRUE)&&GetLastError()!=ERROR_FILE_NOT_FOUND)return false;
  auto size=std::to_wstring(value.display.font_size),theme=std::to_wstring(value.display.theme),schema=std::to_wstring(value.schema),pinyin=std::to_wstring(value.pinyin),double_schema=std::to_wstring(value.double_schema);
  bool ok=WritePrivateProfileStringW(L"display",L"font_size",size.c_str(),temporary.c_str())&&
      WritePrivateProfileStringW(L"display",L"theme",theme.c_str(),temporary.c_str())&&
      WritePrivateProfileStringW(L"input",L"schema",schema.c_str(),temporary.c_str())&&
      WritePrivateProfileStringW(L"input",L"pinyin_schema",pinyin.c_str(),temporary.c_str())&&WritePrivateProfileStringW(L"input",L"double_schema",double_schema.c_str(),temporary.c_str());
  if(ok&&apply_input)ok=WritePrivateProfileStringW(L"input",L"change_id",name,temporary.c_str())!=FALSE;
  if(ok)ok=MoveFileExW(temporary.c_str(),ini.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=FALSE;
  if(!ok)DeleteFileW(temporary.c_str());
  WritePrivateProfileStringW(nullptr,nullptr,nullptr,ini.c_str());return ok;
}
bool EnsureUserSettings(const std::filesystem::path& install,const std::filesystem::path& user){
  if(user.empty())return false;
  std::error_code error;std::filesystem::create_directories(user,error);if(error)return false;
  auto ini=user/L"settings.ini",initial=install/L"settings.ini";
  bool exists=std::filesystem::exists(ini,error);if(error)return false;
  bool has_initial=std::filesystem::exists(initial,error);if(error)return false;
  if(!exists&&has_initial){std::filesystem::copy_file(initial,ini,std::filesystem::copy_options::skip_existing,error);if(error)return false;}
  return true;
}
#include "preferences_surface.hpp"

}
int ApplyInputScheme(const std::filesystem::path& install,const std::filesystem::path& user,uint32_t schema){
  if(schema>=ime::kSchemaCount||!EnsureUserSettings(install,user))return 1;
  Preferences value;value.display=ReadAppearance(user);value.schema=schema;
  value.pinyin=ReadPinyinSchemaPreference(user);value.double_schema=ReadDoubleSchemaPreference(user);
  if(schema){value.pinyin=schema;if(schema>=2)value.double_schema=schema;}
  return SavePreferences(user/L"settings.ini",value,true)?0:1;
}
int PreferencesUI(HINSTANCE instance,int show,const std::filesystem::path& preview_root){
  auto initialized=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);if(FAILED(initialized))return 1;SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);INITCOMMONCONTROLSEX common{sizeof(common),ICC_STANDARD_CLASSES};InitCommonControlsEx(&common);
  View v;v.instance=instance;v.preview_root=preview_root;WNDCLASSW wc{};wc.hInstance=instance;wc.lpfnWndProc=Proc;wc.lpszClassName=L"YANVY.Settings.rc1";wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);wc.hIcon=LoadIconW(instance,MAKEINTRESOURCEW(1));RegisterClassW(&wc);
  UINT dpi=FitSurfaceDpi(nullptr,GetDpiForSystem(),860,620);RECT rect{0,0,MulDiv(860,int(dpi),96),MulDiv(620,int(dpi),96)};DWORD style=WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX;AdjustWindowRectExForDpi(&rect,style,FALSE,0,dpi);
  auto window=CreateWindowExW(WS_EX_CONTROLPARENT,wc.lpszClassName,L"言舟 · 设置",style,CW_USEDEFAULT,CW_USEDEFAULT,rect.right-rect.left,rect.bottom-rect.top,nullptr,nullptr,instance,&v);if(!window){CoUninitialize();return 1;}CenterSurface(window);ShowWindow(window,show);MSG message;while(GetMessageW(&message,nullptr,0,0)>0){if(message.message==WM_KEYDOWN&&message.wParam>='1'&&message.wParam<='3'&&(GetKeyState(VK_CONTROL)&0x8000)){SendMessageW(window,WM_COMMAND,NavInput+message.wParam-'1',0);continue;}if(message.message==WM_KEYDOWN&&message.wParam=='S'&&(GetKeyState(VK_CONTROL)&0x8000)&&IsWindowEnabled(v.save)&&v.page!=2){SendMessageW(window,WM_COMMAND,SaveButton,0);continue;}if(!IsDialogMessageW(window,&message)){TranslateMessage(&message);DispatchMessageW(&message);}}CoUninitialize();return 0;
}
#ifdef YANVY_PREFERENCES_FIXTURE
int PreferencesFixture(const std::filesystem::path& directory){
  if(std::filesystem::exists(directory)||!std::filesystem::create_directory(directory))return 1;
  const auto ini=directory/L"settings.ini";
  if(!WritePrivateProfileStringW(L"custom",L"synthetic",L"preserve-me",ini.c_str()))return 2;
  Preferences value;value.schema=value.pinyin=value.double_schema=4;value.display={32,2};
  if(!SavePreferences(ini,value)||ReadSchemaPreference(directory)!=4||ReadPinyinSchemaPreference(directory)!=4)return 3;
  auto display=ReadAppearance(directory);if(display.font_size!=32||display.theme!=2)return 4;
  if(ReadDoubleSchemaPreference(directory)!=4)return 4;
  if(!SavePreferences(ini,value,true))return 11;
  auto revision=ReadInputRevision(directory);
  if(revision.empty()||!SavePreferences(ini,value,true)||revision==ReadInputRevision(directory))return 12;
  revision=ReadInputRevision(directory);if(!SavePreferences(ini,value,false)||revision!=ReadInputRevision(directory))return 13;
  wchar_t custom[64]{};GetPrivateProfileStringW(L"custom",L"synthetic",L"",custom,64,ini.c_str());if(std::wstring(custom)!=L"preserve-me")return 5;
  for(auto file:std::filesystem::directory_iterator(directory))if(file.path().filename().wstring().find(L"pending-")!=std::wstring::npos)return 6;
  if(SavePreferences(directory/L"missing-parent"/L"settings.ini",value))return 7;
  if(ReadSchemaPreference(directory)!=4)return 8;
  WritePrivateProfileStringW(L"input",L"schema",L"99",ini.c_str());WritePrivateProfileStringW(L"input",L"pinyin_schema",L"999",ini.c_str());
  if(ReadSchemaPreference(directory)!=1||ReadPinyinSchemaPreference(directory)!=1)return 9;
  value.schema=0;value.pinyin=2;if(!SavePreferences(ini,value)||ReadSchemaPreference(directory)!=0||ReadPinyinSchemaPreference(directory)!=2)return 10;
  auto separate=directory/L"separate-user";
  if(ApplyInputScheme(directory,separate,3)!=0||ReadSchemaPreference(separate)!=3||ReadPinyinSchemaPreference(separate)!=3||ReadSchemaPreference(directory)!=0)return 14;
  wchar_t preserved[64]{};GetPrivateProfileStringW(L"custom",L"synthetic",L"",preserved,64,(separate/L"settings.ini").c_str());
  if(std::wstring(preserved)!=L"preserve-me")return 15;
  return 0;
}
#endif
}
