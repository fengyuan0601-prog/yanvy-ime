// SPDX-License-Identifier: BSD-3-Clause
#include "candidate.hpp"
#include <windowsx.h>
#include <dwmapi.h>

namespace yanvy {
CandidateWindow::~CandidateWindow(){
  if(window_)DestroyWindow(window_);
  if(module_)UnregisterClassW(L"YANVY.Candidates.rc2",module_);
}
void CandidateWindow::Hide(){
  hover_=-1;
  if(window_){KillTimer(window_,1);ShowWindow(window_,SW_HIDE);}
}
void CandidateWindow::Update(HINSTANCE module,const std::filesystem::path& root,const Response& state,
    uint32_t schema,bool ascii,RECT caret,bool transient,std::function<void(int)> select,std::function<void()> configure){
  state_=state;SecureZeroMemory(state_.commit,sizeof(state_.commit));
  schema_=schema;ascii_=ascii;caret_=caret;appearance_=ReadAppearance(root);select_=std::move(select);configure_=std::move(configure);hover_=-1;
  if(!transient&&!state.input[0]&&!state.preedit[0]){Hide();return;}
  if(!window_){
    module_=module;
    WNDCLASSW wc{};wc.hInstance=module;wc.lpfnWndProc=Proc;wc.lpszClassName=L"YANVY.Candidates.rc2";
    wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);wc.style=CS_DROPSHADOW;RegisterClassW(&wc);
    auto old=SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    window_=CreateWindowExW(WS_EX_TOPMOST|WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE,wc.lpszClassName,L"言舟候选",
        WS_POPUP,caret.left,caret.bottom,280,240,nullptr,nullptr,module,this);
    SetThreadDpiAwarenessContext(old);
  }
  if(!window_)return;
  Place(GetDpiForWindow(window_));
  KillTimer(window_,1);
  if(transient)SetTimer(window_,1,1400,nullptr);
}
void CandidateWindow::Place(UINT dpi){
  if(!window_)return;
  auto old_dpi=SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
  auto icon=static_cast<HICON>(LoadImageW(module_,MAKEINTRESOURCEW(1),IMAGE_ICON,MulDiv(16,int(dpi),96),MulDiv(16,int(dpi),96),LR_SHARED));
  view_.Set(state_,schema_,ascii_,appearance_,dpi,icon);
  MONITORINFO monitor{sizeof(monitor)};
  if(!GetMonitorInfoW(MonitorFromRect(&caret_,MONITOR_DEFAULTTONEAREST),&monitor))
    monitor.rcWork={0,0,GetSystemMetrics(SM_CXSCREEN),GetSystemMetrics(SM_CYSCREEN)};
  HDC dc=GetDC(window_);SIZE size=view_.Measure(dc,int(monitor.rcWork.right-monitor.rcWork.left),int(monitor.rcWork.bottom-monitor.rcWork.top));ReleaseDC(window_,dc);
  int x=std::clamp<int>(caret_.left,monitor.rcWork.left,monitor.rcWork.right-size.cx);
  int gap=MulDiv(6,int(dpi),96),y=caret_.bottom+gap;
  if(y+size.cy>monitor.rcWork.bottom)y=caret_.top-size.cy-gap;
  y=std::clamp<int>(y,monitor.rcWork.top,std::max<int>(monitor.rcWork.top,monitor.rcWork.bottom-size.cy));
  BOOL dark=view_.Dark();DWORD corner=2;COLORREF border=Palette::Get(dark).border;
  DwmSetWindowAttribute(window_,static_cast<DWMWINDOWATTRIBUTE>(20),&dark,sizeof(dark));
  HRESULT rounded=DwmSetWindowAttribute(window_,static_cast<DWMWINDOWATTRIBUTE>(33),&corner,sizeof(corner));
  DwmSetWindowAttribute(window_,static_cast<DWMWINDOWATTRIBUTE>(34),&border,sizeof(border));
  SetWindowPos(window_,HWND_TOPMOST,x,y,size.cx,size.cy,SWP_NOACTIVATE|SWP_SHOWWINDOW);
  if(FAILED(rounded)){
    int radius=MulDiv(22,int(dpi),96);
    auto region=CreateRoundRectRgn(0,0,size.cx+1,size.cy+1,radius,radius);
    if(!SetWindowRgn(window_,region,TRUE))DeleteObject(region);
  }
  InvalidateRect(window_,nullptr,FALSE);
  SetThreadDpiAwarenessContext(old_dpi);
}
void CandidateWindow::Paint(){
  PAINTSTRUCT paint{};HDC target=BeginPaint(window_,&paint);RECT bounds{};GetClientRect(window_,&bounds);
  HDC buffer=CreateCompatibleDC(target);HBITMAP bitmap=CreateCompatibleBitmap(target,bounds.right,bounds.bottom);
  auto old=SelectObject(buffer,bitmap);
  Fill(buffer,bounds,Palette::Get(view_.Dark()).background);view_.Draw(buffer,bounds,hover_);
  BitBlt(target,0,0,bounds.right,bounds.bottom,buffer,0,0,SRCCOPY);
  SelectObject(buffer,old);DeleteObject(bitmap);DeleteDC(buffer);EndPaint(window_,&paint);
}
LRESULT CALLBACK CandidateWindow::Proc(HWND window,UINT message,WPARAM wp,LPARAM lp){
  auto self=reinterpret_cast<CandidateWindow*>(GetWindowLongPtrW(window,GWLP_USERDATA));
  if(message==WM_NCCREATE){self=static_cast<CandidateWindow*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));}
  if(self){
    if(message==WM_MOUSEACTIVATE)return MA_NOACTIVATE;
    if(message==WM_ERASEBKGND)return 1;
    if(message==WM_PAINT){self->Paint();return 0;}
    if(message==WM_TIMER){self->Hide();return 0;}
    if(message==WM_DPICHANGED){self->Place(HIWORD(wp));return 0;}
    if(message==WM_MOUSEMOVE||message==WM_LBUTTONUP){
      RECT bounds{};GetClientRect(window,&bounds);int index=self->view_.HitTest({GET_X_LPARAM(lp),GET_Y_LPARAM(lp)},bounds);
      bool schema=self->view_.HitSchema({GET_X_LPARAM(lp),GET_Y_LPARAM(lp)},bounds);
      if(message==WM_LBUTTONUP&&schema){if(self->configure_)self->configure_();return 0;}
      if(message==WM_LBUTTONUP){if(index>=0&&self->select_)self->select_(index);return 0;}
      if(self->hover_!=index){self->hover_=index;InvalidateRect(window,nullptr,FALSE);}
      TRACKMOUSEEVENT track{sizeof(track),TME_LEAVE,window,0};TrackMouseEvent(&track);
      SetCursor(LoadCursorW(nullptr,index>=0||schema?IDC_HAND:IDC_ARROW));return 0;
    }
    if(message==WM_MOUSELEAVE){self->hover_=-1;InvalidateRect(window,nullptr,FALSE);return 0;}
  }
  return DefWindowProcW(window,message,wp,lp);
}
}
