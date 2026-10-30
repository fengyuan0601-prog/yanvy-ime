// SPDX-License-Identifier: BSD-3-Clause
#pragma once
#include "appearance.hpp"
#include <commctrl.h>
#include <dwmapi.h>

namespace yanvy {
inline void FocusOutline(HDC dc,RECT r,int radius,COLORREF color){
  Gdiplus::Graphics graphics(dc);graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
  const float x=float(r.left)+1.f,y=float(r.top)+1.f,w=float(r.right-r.left)-2.f,h=float(r.bottom-r.top)-2.f,d=float(radius*2);
  Gdiplus::GraphicsPath path;path.AddArc(x,y,d,d,180,90);path.AddArc(x+w-d,y,d,d,270,90);path.AddArc(x+w-d,y+h-d,d,d,0,90);path.AddArc(x,y+h-d,d,d,90,90);path.CloseFigure();
  Gdiplus::Pen pen(Gdiplus::Color(255,GetRValue(color),GetGValue(color),GetBValue(color)),1.2f);graphics.DrawPath(&pen,&path);
}
// Native buttons retain keyboard activation, focus and accessibility names.
// Only their surface is painted; hover never changes input or saved settings.
inline LRESULT CALLBACK SurfaceControlProc(HWND window,UINT message,WPARAM wp,LPARAM lp,UINT_PTR id,DWORD_PTR){
  if(message==WM_PAINT&&GetPropW(window,L"YANVY.Surface.Combo")){
    PAINTSTRUCT paint{};auto dc=BeginPaint(window,&paint);RECT r{};GetClientRect(window,&r);
    const bool dark=GetPropW(window,L"YANVY.Surface.Dark")!=nullptr;const auto p=Palette::Get(dark);const int dpi=int(INT_PTR(GetPropW(window,L"YANVY.Surface.Dpi")));auto D=[&](int x){return MulDiv(x,dpi?dpi:96,96);};
    Fill(dc,r,p.background);const bool focus=GetFocus()==window;Rounded(dc,r,D(6),p.background,focus?p.accent:p.border);
    wchar_t text[128]{};const LRESULT selected=SendMessageW(window,CB_GETCURSEL,0,0);if(selected>=0)SendMessageW(window,CB_GETLBTEXT,selected,reinterpret_cast<LPARAM>(text));
    RECT label=r;label.left+=D(12);label.right-=D(28);Label(dc,text,label,reinterpret_cast<HFONT>(SendMessageW(window,WM_GETFONT,0,0)),IsWindowEnabled(window)?p.text:p.muted);
    auto pen=CreatePen(PS_SOLID,std::max(1,D(1)),p.muted);auto old=SelectObject(dc,pen);int x=r.right-D(16),y=(r.top+r.bottom)/2;MoveToEx(dc,x-D(4),y-D(2),nullptr);LineTo(dc,x,y+D(2));LineTo(dc,x+D(4),y-D(2));SelectObject(dc,old);DeleteObject(pen);EndPaint(window,&paint);return 0;
  }
  if(message==WM_MOUSEMOVE){
    if(!GetPropW(window,L"YANVY.Surface.Hot")){
      SetPropW(window,L"YANVY.Surface.Hot",reinterpret_cast<HANDLE>(1));
      TRACKMOUSEEVENT track{sizeof(track),TME_LEAVE,window,0};TrackMouseEvent(&track);InvalidateRect(window,nullptr,FALSE);
    }
  }
  if(message==WM_MOUSELEAVE||message==WM_ENABLE){RemovePropW(window,L"YANVY.Surface.Hot");InvalidateRect(window,nullptr,FALSE);}
  if(message==WM_SETFOCUS||message==WM_KILLFOCUS)InvalidateRect(window,nullptr,FALSE);
  if(message==WM_NCDESTROY){for(auto name:{L"YANVY.Surface.Hot",L"YANVY.Surface.Combo",L"YANVY.Surface.Dark",L"YANVY.Surface.Dpi"})RemovePropW(window,name);RemoveWindowSubclass(window,SurfaceControlProc,id);}
  return DefSubclassProc(window,message,wp,lp);
}
inline void TrackSurface(HWND window){SetWindowSubclass(window,SurfaceControlProc,0x594156,0);}
inline bool SurfaceHot(HWND window){return GetPropW(window,L"YANVY.Surface.Hot")!=nullptr;}
inline void StyleCombo(HWND window,bool dark,UINT dpi){SetPropW(window,L"YANVY.Surface.Combo",reinterpret_cast<HANDLE>(1));SetPropW(window,L"YANVY.Surface.Dark",reinterpret_cast<HANDLE>(INT_PTR(dark)));SetPropW(window,L"YANVY.Surface.Dpi",reinterpret_cast<HANDLE>(INT_PTR(dpi)));TrackSurface(window);InvalidateRect(window,nullptr,FALSE);}
inline COLORREF Canvas(bool dark=false){return dark?RGB(18,26,35):RGB(245,248,250);}
inline void SurfaceButton(HDC dc,RECT rect,const wchar_t* text,HFONT font,int radius,const Palette& p,bool primary,bool disabled,bool pressed,bool hot,bool focus){
  const bool dark=GetRValue(p.background)<80;
  COLORREF fill=primary?(disabled?p.line:dark?(pressed?RGB(85,184,166):hot?RGB(139,228,212):p.accent):pressed?RGB(7,85,82):hot?RGB(9,111,105):p.accent):(pressed?p.selection:hot?p.hover:p.background);
  Rounded(dc,rect,radius,fill,primary?fill:focus?p.accent:p.border);
  Label(dc,text,rect,font,disabled?p.muted:primary?p.badge_text:p.text,DT_CENTER);
  if(focus){InflateRect(&rect,-3,-3);FocusOutline(dc,rect,std::max(2,radius-2),primary?p.badge_text:p.accent);}
}
inline UINT FitSurfaceDpi(HWND window,UINT requested,int width,int height){
  MONITORINFO info{sizeof(info)};if(!GetMonitorInfoW(MonitorFromWindow(window,MONITOR_DEFAULTTONEAREST),&info))return requested;
  int max_x=MulDiv(info.rcWork.right-info.rcWork.left-24,96,width);
  int max_y=MulDiv(info.rcWork.bottom-info.rcWork.top-56,96,height);
  return std::max<UINT>(72,std::min<UINT>(requested,UINT(std::max(72,std::min(max_x,max_y)))));
}
inline void CenterSurface(HWND window){
  MONITORINFO info{sizeof(info)};RECT r{};if(!GetMonitorInfoW(MonitorFromWindow(window,MONITOR_DEFAULTTONEAREST),&info)||!GetWindowRect(window,&r))return;
  const int x=info.rcWork.left+(info.rcWork.right-info.rcWork.left-(r.right-r.left))/2;
  const int y=info.rcWork.top+(info.rcWork.bottom-info.rcWork.top-(r.bottom-r.top))/2;
  SetWindowPos(window,nullptr,x,y,0,0,SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE);
}
}
