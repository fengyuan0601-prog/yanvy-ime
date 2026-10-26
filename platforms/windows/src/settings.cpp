// SPDX-License-Identifier: BSD-3-Clause
#include "candidate_view.hpp"
#include <commctrl.h>
#include <dwmapi.h>
#include <shellapi.h>

namespace {
HWND size_box=nullptr,theme_box=nullptr,save_button=nullptr,reset_button=nullptr,help_button=nullptr;
HWND size_label=nullptr,theme_label=nullptr,status_label=nullptr;
HBRUSH card_brush=nullptr,background_brush=nullptr;
HFONT body=nullptr,title=nullptr,caption=nullptr;
HICON icon=nullptr;UINT dpi=96;bool dirty=false;std::wstring status;
std::filesystem::path ini;yanvy::Appearance saved,current;
yanvy::CandidateView preview;
int D(int value){return MulDiv(value,int(dpi),96);}
void Preview(){
  yanvy::Response state;state.count=2;state.last_page=1;
  wcscpy_s(state.input,L"wqvb");wcscpy_s(state.preedit,L"wqvb");
  wcscpy_s(state.candidates[0],L"你好");wcscpy_s(state.candidates[1],L"您好");
  preview.Set(state,false,false,current,dpi,icon);
}
void MarkChanged(HWND window){
  current.font_size=12+int(SendMessageW(size_box,CB_GETCURSEL,0,0));
  current.theme=int(SendMessageW(theme_box,CB_GETCURSEL,0,0));
  dirty=current.font_size!=saved.font_size||current.theme!=saved.theme;
  status=dirty?L"预览已更新，保存后生效。":L"更改会在下一次显示候选窗时生效。";
  SetWindowTextW(status_label,status.c_str());
  EnableWindow(save_button,dirty);Preview();InvalidateRect(window,nullptr,FALSE);
}
HWND Control(HWND parent,const wchar_t* type,const wchar_t* text,DWORD style,int id){
  return CreateWindowExW(0,type,text,WS_CHILD|WS_VISIBLE|style,0,0,1,1,parent,reinterpret_cast<HMENU>(INT_PTR(id)),GetModuleHandleW(nullptr),nullptr);
}
void Layout(HWND window){
  for(auto font:{body,title,caption})if(font)DeleteObject(font);
  body=yanvy::Font(D(15));title=yanvy::Font(D(25),FW_SEMIBOLD);caption=yanvy::Font(D(12));
  icon=static_cast<HICON>(LoadImageW(GetModuleHandleW(nullptr),MAKEINTRESOURCEW(1),IMAGE_ICON,D(44),D(44),LR_SHARED));
  for(auto control:{size_box,theme_box,save_button,reset_button,help_button})SendMessageW(control,WM_SETFONT,reinterpret_cast<WPARAM>(body),TRUE);
  for(auto control:{size_label,theme_label})SendMessageW(control,WM_SETFONT,reinterpret_cast<WPARAM>(body),TRUE);
  SendMessageW(status_label,WM_SETFONT,reinterpret_cast<WPARAM>(caption),TRUE);
  SetWindowPos(size_label,nullptr,D(44),D(119),D(294),D(30),SWP_NOZORDER|SWP_NOACTIVATE);
  SetWindowPos(theme_label,nullptr,D(44),D(187),D(294),D(30),SWP_NOZORDER|SWP_NOACTIVATE);
  SetWindowPos(status_label,nullptr,D(27),D(580),D(527),D(24),SWP_NOZORDER|SWP_NOACTIVATE);
  SetWindowPos(size_box,nullptr,D(382),D(122),D(156),D(300),SWP_NOZORDER|SWP_NOACTIVATE);
  SetWindowPos(theme_box,nullptr,D(382),D(184),D(156),D(200),SWP_NOZORDER|SWP_NOACTIVATE);
  SendMessageW(size_box,CB_SETITEMHEIGHT,WPARAM(-1),D(26));
  SendMessageW(theme_box,CB_SETITEMHEIGHT,WPARAM(-1),D(26));
  SetWindowPos(reset_button,nullptr,D(26),D(530),D(104),D(38),SWP_NOZORDER|SWP_NOACTIVATE);
  SetWindowPos(help_button,nullptr,D(142),D(530),D(104),D(38),SWP_NOZORDER|SWP_NOACTIVATE);
  SetWindowPos(save_button,nullptr,D(410),D(530),D(144),D(38),SWP_NOZORDER|SWP_NOACTIVATE);
  Preview();InvalidateRect(window,nullptr,FALSE);
}
void Paint(HWND window){
  PAINTSTRUCT paint{};HDC dc=BeginPaint(window,&paint);RECT bounds{};GetClientRect(window,&bounds);
  HDC buffer=CreateCompatibleDC(dc);HBITMAP bitmap=CreateCompatibleBitmap(dc,bounds.right,bounds.bottom);
  auto original=SelectObject(buffer,bitmap);auto p=yanvy::Palette::Get(false);
  yanvy::Fill(buffer,bounds,RGB(245,248,251));
  if(icon)DrawIconEx(buffer,D(26),D(26),icon,D(44),D(44),0,nullptr,DI_NORMAL);
  yanvy::Label(buffer,L"言舟",{D(84),D(20),D(310),D(56)},title,p.text);
  yanvy::Label(buffer,L"输入与外观",{D(85),D(58),D(330),D(83)},caption,p.muted);
  yanvy::Label(buffer,L"0.1.0-dev.2",{D(392),D(33),D(552),D(63)},caption,p.muted,DT_RIGHT);
  yanvy::Rounded(buffer,{D(24),D(104),D(556),D(246)},D(12),p.background,p.border);
  yanvy::Label(buffer,L"调整候选文字的显示大小",{D(44),D(149),D(352),D(169)},caption,p.muted);
  yanvy::Fill(buffer,{D(44),D(176),D(536),D(177)},p.line);
  yanvy::Label(buffer,L"浅色、深色，或跟随系统",{D(44),D(215),D(354),D(235)},caption,p.muted);
  yanvy::Label(buffer,L"外观预览",{D(26),D(262),D(330),D(288)},body,p.text);
  yanvy::Label(buffer,L"示例文字",{D(392),D(262),D(554),D(288)},caption,p.muted,DT_RIGHT);
  RECT preview_area{D(24),D(300),D(556),D(520)};
  yanvy::Rounded(buffer,preview_area,D(12),RGB(234,241,246),RGB(226,234,240));
  auto size=preview.Measure(buffer,D(492));
  int x=(preview_area.left+preview_area.right-size.cx)/2,y=(preview_area.top+preview_area.bottom-size.cy)/2;
  preview.Draw(buffer,{x,y,x+size.cx,y+size.cy});
  BitBlt(dc,0,0,bounds.right,bounds.bottom,buffer,0,0,SRCCOPY);
  SelectObject(buffer,original);DeleteObject(bitmap);DeleteDC(buffer);EndPaint(window,&paint);
}
void Button(const DRAWITEMSTRUCT& item){
  bool primary=item.CtlID==104,disabled=(item.itemState&ODS_DISABLED)!=0,pressed=(item.itemState&ODS_SELECTED)!=0;
  auto p=yanvy::Palette::Get(false);
  auto color=primary?(disabled?RGB(220,230,235):(pressed?RGB(8,87,88):p.accent)):(pressed?p.hover:p.background);
  yanvy::Rounded(item.hDC,item.rcItem,D(8),color,primary?color:p.border);
  wchar_t text[64]{};GetWindowTextW(item.hwndItem,text,64);
  yanvy::Label(item.hDC,text,item.rcItem,body,primary?(disabled?p.muted:RGB(255,255,255)):p.text,DT_CENTER);
  if(item.itemState&ODS_FOCUS){RECT focus=item.rcItem;InflateRect(&focus,-D(4),-D(4));DrawFocusRect(item.hDC,&focus);}
}
LRESULT CALLBACK Proc(HWND window,UINT message,WPARAM wp,LPARAM lp){
  if(message==WM_CREATE){
    dpi=GetDpiForWindow(window);ini=yanvy::ModuleDir()/L"settings.ini";
    saved=current=yanvy::ReadAppearance(yanvy::ModuleDir());status=L"更改会在下一次显示候选窗时生效。";
    card_brush=CreateSolidBrush(yanvy::Palette::Get(false).background);background_brush=CreateSolidBrush(RGB(245,248,251));
    size_label=Control(window,L"STATIC",L"候选字号",SS_LEFT|SS_CENTERIMAGE,106);
    size_box=Control(window,L"COMBOBOX",L"候选字号",CBS_DROPDOWNLIST|WS_TABSTOP|WS_VSCROLL,101);
    theme_label=Control(window,L"STATIC",L"候选配色",SS_LEFT|SS_CENTERIMAGE,107);
    theme_box=Control(window,L"COMBOBOX",L"候选配色",CBS_DROPDOWNLIST|WS_TABSTOP,102);
    status_label=Control(window,L"STATIC",status.c_str(),SS_LEFT|SS_CENTERIMAGE,108);
    for(int value=12;value<=32;++value)SendMessageW(size_box,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(std::to_wstring(value).c_str()));
    for(auto text:{L"跟随系统",L"浅色",L"深色"})SendMessageW(theme_box,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(text));
    SendMessageW(size_box,CB_SETCURSEL,current.font_size-12,0);SendMessageW(theme_box,CB_SETCURSEL,current.theme,0);
    reset_button=Control(window,L"BUTTON",L"恢复默认",BS_OWNERDRAW|WS_TABSTOP,105);
    help_button=Control(window,L"BUTTON",L"使用说明",BS_OWNERDRAW|WS_TABSTOP,103);
    save_button=Control(window,L"BUTTON",L"保存更改",BS_OWNERDRAW|WS_TABSTOP,104);
    EnableWindow(save_button,FALSE);Layout(window);
    BOOL light=FALSE;DwmSetWindowAttribute(window,static_cast<DWMWINDOWATTRIBUTE>(20),&light,sizeof(light));
    return 0;
  }
  if(message==WM_ERASEBKGND)return 1;
  if(message==WM_CTLCOLORSTATIC){
    HDC dc=reinterpret_cast<HDC>(wp);bool status_control=reinterpret_cast<HWND>(lp)==status_label;
    SetBkMode(dc,TRANSPARENT);auto p=yanvy::Palette::Get(false);
    SetTextColor(dc,status_control?(status==L"已保存"?p.accent:p.muted):p.text);
    return reinterpret_cast<LRESULT>(status_control?background_brush:card_brush);
  }
  if(message==WM_PAINT){Paint(window);return 0;}
  if(message==WM_DRAWITEM){Button(*reinterpret_cast<DRAWITEMSTRUCT*>(lp));return TRUE;}
  if(message==WM_DPICHANGED){
    dpi=HIWORD(wp);auto rect=reinterpret_cast<RECT*>(lp);
    SetWindowPos(window,nullptr,rect->left,rect->top,rect->right-rect->left,rect->bottom-rect->top,SWP_NOZORDER|SWP_NOACTIVATE);
    Layout(window);return 0;
  }
  if(message==WM_COMMAND){
    if((LOWORD(wp)==101||LOWORD(wp)==102)&&HIWORD(wp)==CBN_SELCHANGE){MarkChanged(window);return 0;}
    if(LOWORD(wp)==105){SendMessageW(size_box,CB_SETCURSEL,6,0);SendMessageW(theme_box,CB_SETCURSEL,0,0);MarkChanged(window);return 0;}
    if(LOWORD(wp)==104){
      auto size=std::to_wstring(current.font_size),theme=std::to_wstring(current.theme);
      bool ok=WritePrivateProfileStringW(L"display",L"font_size",size.c_str(),ini.c_str())&&WritePrivateProfileStringW(L"display",L"theme",theme.c_str(),ini.c_str());
      if(ok){saved=current;dirty=false;EnableWindow(save_button,FALSE);status=L"已保存";}
      else status=L"保存失败，请检查此安装目录的写入权限。";
      SetWindowTextW(status_label,status.c_str());
      InvalidateRect(window,nullptr,FALSE);return 0;
    }
    if(LOWORD(wp)==103){auto help=yanvy::ModuleDir()/L"TESTING.txt";ShellExecuteW(window,L"open",help.c_str(),nullptr,nullptr,SW_SHOWNORMAL);return 0;}
  }
  if(message==WM_DESTROY){for(auto font:{body,title,caption})if(font)DeleteObject(font);DeleteObject(card_brush);DeleteObject(background_brush);PostQuitMessage(0);return 0;}
  return DefWindowProcW(window,message,wp,lp);
}
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,PWSTR,int show){
  SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
  INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_STANDARD_CLASSES};InitCommonControlsEx(&controls);
  WNDCLASSW wc{};wc.hInstance=instance;wc.lpfnWndProc=Proc;wc.lpszClassName=L"YANVY.Settings.dev2";
  wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);wc.hIcon=LoadIconW(instance,MAKEINTRESOURCEW(1));RegisterClassW(&wc);
  UINT initial=GetDpiForSystem();RECT rect{0,0,MulDiv(580,int(initial),96),MulDiv(620,int(initial),96)};
  DWORD style=WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX;
  AdjustWindowRectExForDpi(&rect,style,FALSE,0,initial);
  HWND window=CreateWindowExW(0,wc.lpszClassName,L"言舟 · 输入与外观",style,CW_USEDEFAULT,CW_USEDEFAULT,
      rect.right-rect.left,rect.bottom-rect.top,nullptr,nullptr,instance,nullptr);
  if(!window)return 1;
  ShowWindow(window,show);MSG message;
  while(GetMessageW(&message,nullptr,0,0)>0)if(!IsDialogMessageW(window,&message)){TranslateMessage(&message);DispatchMessageW(&message);}
  return 0;
}
