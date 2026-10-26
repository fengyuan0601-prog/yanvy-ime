// SPDX-License-Identifier: BSD-3-Clause
#include "common.hpp"
#include <algorithm>
#include <shellapi.h>

namespace {
HWND size_box=nullptr,theme_box=nullptr;
HFONT font=nullptr;
std::filesystem::path ini;
HWND Control(HWND parent,const wchar_t* type,const wchar_t* text,DWORD style,int x,int y,int width,int height,int id) {
  HWND control = CreateWindowExW(0,type,text,WS_CHILD|WS_VISIBLE|style,x,y,width,height,parent,
      reinterpret_cast<HMENU>(INT_PTR(id)),GetModuleHandleW(nullptr),nullptr);
  SendMessageW(control,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);
  return control;
}
LRESULT CALLBACK Proc(HWND window,UINT message,WPARAM wp,LPARAM lp) {
  if (message==WM_CREATE) {
    font=CreateFontW(-18,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Microsoft YaHei UI");
    ini=yanvy::ModuleDir()/L"settings.ini";
    Control(window,L"STATIC",L"言舟 · YANVY",0,28,22,390,34,0);
    Control(window,L"STATIC",L"0.1.0-dev.1  开发测试 · 固定离线词库 · 无学习",0,28,62,410,30,0);
    Control(window,L"STATIC",L"候选字号",0,28,112,130,30,0);
    size_box=Control(window,L"COMBOBOX",L"",CBS_DROPDOWNLIST|WS_TABSTOP|WS_VSCROLL,180,108,220,260,101);
    int size=std::clamp<int>(GetPrivateProfileIntW(L"display",L"font_size",18,ini.c_str()),12,32);
    for(int value=12;value<=32;++value) SendMessageW(size_box,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(std::to_wstring(value).c_str()));
    SendMessageW(size_box,CB_SETCURSEL,size-12,0);
    Control(window,L"STATIC",L"候选配色",0,28,164,130,30,0);
    theme_box=Control(window,L"COMBOBOX",L"",CBS_DROPDOWNLIST|WS_TABSTOP,180,160,220,180,102);
    for(auto text:{L"跟随系统",L"浅色",L"深色"}) SendMessageW(theme_box,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(text));
    SendMessageW(theme_box,CB_SETCURSEL,std::clamp<int>(GetPrivateProfileIntW(L"display",L"theme",0,ini.c_str()),0,2),0);
    Control(window,L"BUTTON",L"测试说明",WS_TABSTOP,28,232,118,38,103);
    Control(window,L"BUTTON",L"保存",BS_DEFPUSHBUTTON|WS_TABSTOP,282,232,118,38,104);
    return 0;
  }
  if(message==WM_COMMAND) {
    if(LOWORD(wp)==104) {
      auto size=std::to_wstring(12+SendMessageW(size_box,CB_GETCURSEL,0,0));
      auto theme=std::to_wstring(SendMessageW(theme_box,CB_GETCURSEL,0,0));
      bool ok=WritePrivateProfileStringW(L"display",L"font_size",size.c_str(),ini.c_str()) &&
          WritePrivateProfileStringW(L"display",L"theme",theme.c_str(),ini.c_str());
      MessageBoxW(window,ok?L"已保存。下一次显示候选窗时生效。":L"无法保存，请检查安装目录的写入权限。",L"言舟",ok?MB_OK:MB_OK|MB_ICONERROR);
    } else if(LOWORD(wp)==103) {
      auto help=yanvy::ModuleDir()/L"TESTING.txt";
      ShellExecuteW(window,L"open",help.c_str(),nullptr,nullptr,SW_SHOWNORMAL);
    }
    return 0;
  }
  if(message==WM_DESTROY) { if(font)DeleteObject(font);PostQuitMessage(0);return 0; }
  return DefWindowProcW(window,message,wp,lp);
}
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,PWSTR,int show) {
  SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_SYSTEM_AWARE);
  WNDCLASSW wc{}; wc.hInstance=instance;wc.lpfnWndProc=Proc;wc.lpszClassName=L"YANVY.Settings.dev1";
  wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);wc.hbrBackground=reinterpret_cast<HBRUSH>(COLOR_WINDOW+1);
  wc.hIcon=LoadIconW(instance,MAKEINTRESOURCEW(1));RegisterClassW(&wc);
  HWND window=CreateWindowExW(0,wc.lpszClassName,L"言舟 · YANVY 设置",WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX,
      CW_USEDEFAULT,CW_USEDEFAULT,460,340,nullptr,nullptr,instance,nullptr);
  if(!window)return 1;
  ShowWindow(window,show);MSG message;
  while(GetMessageW(&message,nullptr,0,0)>0) { if(!IsDialogMessageW(window,&message)){TranslateMessage(&message);DispatchMessageW(&message);} }
  return 0;
}
