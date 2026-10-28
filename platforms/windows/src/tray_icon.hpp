// SPDX-License-Identifier: BSD-3-Clause
#pragma once
#include "common.hpp"
#include <shellapi.h>
#include <atomic>

namespace yanvy {
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
  bool RemoveForTest() { return Remove(); }
 private:
  static constexpr UINT callback=WM_APP+41;
  void Add(){
    NOTIFYICONDATAW data{sizeof(data)};data.hWnd=window_;data.uID=1;data.uFlags=NIF_MESSAGE|NIF_ICON|NIF_TIP;
    data.uCallbackMessage=callback;data.hIcon=LoadIconW(GetModuleHandleW(nullptr),MAKEINTRESOURCEW(1));
    wcscpy_s(data.szTip,L"言舟 · 右键打开设置");added_=Shell_NotifyIconW(NIM_ADD,&data)!=FALSE;
  }
  bool Remove(){NOTIFYICONDATAW data{sizeof(data)};data.hWnd=window_;data.uID=1;bool removed=Shell_NotifyIconW(NIM_DELETE,&data)!=FALSE;added_=false;return removed;}
  static LRESULT CALLBACK Proc(HWND window,UINT message,WPARAM wp,LPARAM lp){
    auto self=reinterpret_cast<TrayIcon*>(GetWindowLongPtrW(window,GWLP_USERDATA));
    if(message==WM_NCCREATE){self=static_cast<TrayIcon*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));}
    if(self){
      if(message==self->taskbar_created_&&!self->hidden_){self->Add();return 0;}
      if(message==callback&&lp==WM_LBUTTONDBLCLK){OpenSettings(ModuleDir());return 0;}
      if(message==callback&&(lp==WM_RBUTTONUP||lp==WM_CONTEXTMENU)){
        HMENU menu=CreatePopupMenu();if(!menu)return 0;
        AppendMenuW(menu,MF_STRING,1,L"言舟设置");AppendMenuW(menu,MF_STRING,2,L"隐藏图标（本次运行）");
        POINT p{};GetCursorPos(&p);SetForegroundWindow(window);
        UINT selected=TrackPopupMenu(menu,TPM_RIGHTBUTTON|TPM_RETURNCMD|TPM_NONOTIFY,p.x,p.y,0,window,nullptr);
        DestroyMenu(menu);PostMessageW(window,WM_NULL,0,0);
        if(selected==1)OpenSettings(ModuleDir());else if(selected==2){self->hidden_=true;self->Remove();}
        return 0;
      }
      if(message==WM_CLOSE){DestroyWindow(window);return 0;}
      if(message==WM_DESTROY){self->Remove();PostQuitMessage(0);return 0;}
    }
    return DefWindowProcW(window,message,wp,lp);
  }
  static DWORD WINAPI ThreadMain(void* value){
    auto self=static_cast<TrayIcon*>(value);HINSTANCE instance=GetModuleHandleW(nullptr);
    WNDCLASSW wc{};wc.hInstance=instance;wc.lpfnWndProc=Proc;wc.lpszClassName=L"YANVY.Tray.rc4";
    RegisterClassW(&wc);self->taskbar_created_=RegisterWindowMessageW(L"TaskbarCreated");
    self->window_=CreateWindowExW(WS_EX_TOOLWINDOW,wc.lpszClassName,L"言舟",WS_OVERLAPPED,0,0,0,0,nullptr,nullptr,instance,self);
    if(self->window_)self->Add();
    SetEvent(self->ready_);
    if(self->window_){MSG message;while(GetMessageW(&message,nullptr,0,0)>0){TranslateMessage(&message);DispatchMessageW(&message);}}
    UnregisterClassW(wc.lpszClassName,instance);return 0;
  }
  HANDLE ready_=nullptr,thread_=nullptr;DWORD thread_id_=0;HWND window_=nullptr;UINT taskbar_created_=0;bool hidden_=false;std::atomic_bool added_{false};
};
}
