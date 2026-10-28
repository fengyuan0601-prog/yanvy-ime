// SPDX-License-Identifier: BSD-3-Clause
#pragma once
#include "common.hpp"
#include <shellapi.h>
#include <windowsx.h>
#include <atomic>

namespace yanvy {
enum class TrayAction { None, Settings, Menu };
inline TrayAction TrayActionFor(bool version4, WPARAM wp, LPARAM lp) {
  const UINT id = version4 ? HIWORD(lp) : static_cast<UINT>(wp);
  if (id != 1) return TrayAction::None;
  const UINT event = version4 ? LOWORD(lp) : static_cast<UINT>(lp);
  if ((version4 && (event == NIN_SELECT || event == NIN_KEYSELECT)) ||
      (!version4 && event == WM_LBUTTONUP)) return TrayAction::Settings;
  if ((version4 && event == WM_CONTEXTMENU) || (!version4 && event == WM_RBUTTONUP)) return TrayAction::Menu;
  return TrayAction::None;
}
inline bool TrayMenuAnchorFor(bool version4,WPARAM wp,const RECT* icon,const POINT* cursor,POINT& point) {
  // Version 4 context-menu callbacks carry the click point in wParam, as in
  // Microsoft's NotificationIcon sample. Keep signed desktop coordinates:
  // an overflow-panel click is not necessarily at the taskbar icon rectangle.
  if(version4){
    const POINT callback_point{GET_X_LPARAM(wp),GET_Y_LPARAM(wp)};
    if(callback_point.x!=-1||callback_point.y!=-1){point=callback_point;return true;}
  }
  // Keyboard context-menu invocations may use the (-1,-1) sentinel. Anchor
  // those at the icon; legacy callbacks carry only an icon ID in wParam.
  if(version4&&icon&&icon->left<icon->right&&icon->top<icon->bottom){
    point={icon->left+(icon->right-icon->left)/2,icon->top+(icon->bottom-icon->top)/2};return true;
  }
  if(cursor){point=*cursor;return true;}
  return false;
}
class TrayDpiContext {
 public:
  TrayDpiContext():previous_(SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)){}
  ~TrayDpiContext(){if(previous_)SetThreadDpiAwarenessContext(previous_);}
  TrayDpiContext(const TrayDpiContext&)=delete;
  TrayDpiContext& operator=(const TrayDpiContext&)=delete;
 private:DPI_AWARENESS_CONTEXT previous_;
};
// One icon for the per-user broker; the input indicator itself is Windows UI.
class TrayIcon {
 public:
  TrayIcon(){
    ready_=CreateEventW(nullptr,TRUE,FALSE,nullptr);
    if(ready_) thread_=CreateThread(nullptr,0,ThreadMain,this,0,&thread_id_);
    if(thread_)WaitForSingleObject(ready_,INFINITE);
  }
  ~TrayIcon(){
    if(thread_){if(WaitForSingleObject(thread_,0)!=WAIT_OBJECT_0){if(window_)PostMessageW(window_,WM_CLOSE,0,0);else PostThreadMessageW(thread_id_,WM_QUIT,0,0);}
      WaitForSingleObject(thread_,INFINITE);CloseHandle(thread_);}
    if(ready_)CloseHandle(ready_);
  }
  TrayIcon(const TrayIcon&)=delete;
  TrayIcon& operator=(const TrayIcon&)=delete;
  bool Registered() const { return added_.load(); }
  bool Version4() const { return version4_.load(); }
  bool RemoveForTest() { return Remove(); }
 private:
  static constexpr UINT callback=WM_APP+41;
  void Add(){
    NOTIFYICONDATAW data{sizeof(data)};data.hWnd=window_;data.uID=1;data.uFlags=NIF_MESSAGE|NIF_ICON|NIF_TIP|NIF_SHOWTIP;
    data.uCallbackMessage=callback;data.hIcon=LoadIconW(GetModuleHandleW(nullptr),MAKEINTRESOURCEW(1));
    wcscpy_s(data.szTip,L"言舟设置 · 单击打开，右键菜单");
    added_=Shell_NotifyIconW(NIM_ADD,&data)!=FALSE;
    version4_=false;
    if(added_){data.uVersion=NOTIFYICON_VERSION_4;version4_=Shell_NotifyIconW(NIM_SETVERSION,&data)!=FALSE;}
  }
  bool Remove(){NOTIFYICONDATAW data{sizeof(data)};data.hWnd=window_;data.uID=1;bool removed=Shell_NotifyIconW(NIM_DELETE,&data)!=FALSE;added_=false;return removed;}
  static LRESULT CALLBACK Proc(HWND window,UINT message,WPARAM wp,LPARAM lp){
    auto self=reinterpret_cast<TrayIcon*>(GetWindowLongPtrW(window,GWLP_USERDATA));
    if(message==WM_NCCREATE){self=static_cast<TrayIcon*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));}
    if(self){
      if(message==self->taskbar_created_&&!self->hidden_){self->Add();return 0;}
      if(message==callback){
        const auto action=TrayActionFor(self->version4_,wp,lp);
        if(action==TrayAction::Settings){OpenSettings(ModuleDir());return 0;}
        if(action==TrayAction::Menu){
        HMENU menu=CreatePopupMenu();if(!menu)return 0;
        AppendMenuW(menu,MF_STRING,1,L"言舟设置");AppendMenuW(menu,MF_STRING,2,L"隐藏图标（本次运行）");
        POINT p{},cursor{};RECT rect{};
        NOTIFYICONIDENTIFIER id{sizeof(id)};id.hWnd=window;id.uID=1;
        const bool has_icon=SUCCEEDED(Shell_NotifyIconGetRect(&id,&rect));
        const bool has_cursor=GetCursorPos(&cursor)!=FALSE;
        if(!TrayMenuAnchorFor(self->version4_,wp,has_icon?&rect:nullptr,has_cursor?&cursor:nullptr,p)){DestroyMenu(menu);return 0;}
        SetForegroundWindow(window);
        UINT selected=TrackPopupMenu(menu,TPM_RIGHTBUTTON|TPM_RETURNCMD|TPM_NONOTIFY,p.x,p.y,0,window,nullptr);
        DestroyMenu(menu);PostMessageW(window,WM_NULL,0,0);
        if(selected==1)OpenSettings(ModuleDir());else if(selected==2){self->hidden_=true;self->Remove();}
        return 0;
        }
      }
      if(message==WM_CLOSE){DestroyWindow(window);return 0;}
      if(message==WM_DESTROY){self->Remove();PostQuitMessage(0);return 0;}
    }
    return DefWindowProcW(window,message,wp,lp);
  }
  static DWORD WINAPI ThreadMain(void* value){
    // The HWND retains its creator's DPI context for every callback. Set this
    // before creating it so shell/cursor coordinates and popup placement all
    // use physical screen pixels, without a second DPI-virtualization scale.
    // Only this tray thread changes; the broker's other threads are untouched.
    TrayDpiContext dpi;
    auto self=static_cast<TrayIcon*>(value);HINSTANCE instance=GetModuleHandleW(nullptr);
    WNDCLASSW wc{};wc.hInstance=instance;wc.lpfnWndProc=Proc;wc.lpszClassName=L"YANVY.Tray.rc4";
    RegisterClassW(&wc);self->taskbar_created_=RegisterWindowMessageW(L"TaskbarCreated");
    self->window_=CreateWindowExW(WS_EX_TOOLWINDOW,wc.lpszClassName,L"言舟",WS_OVERLAPPED,0,0,0,0,nullptr,nullptr,instance,self);
    if(self->window_)self->Add();
    SetEvent(self->ready_);
    if(self->window_){MSG message;while(GetMessageW(&message,nullptr,0,0)>0){TranslateMessage(&message);DispatchMessageW(&message);}}
    UnregisterClassW(wc.lpszClassName,instance);return 0;
  }
  HANDLE ready_=nullptr,thread_=nullptr;DWORD thread_id_=0;HWND window_=nullptr;UINT taskbar_created_=0;bool hidden_=false;std::atomic_bool added_{false},version4_{false};
};
}
