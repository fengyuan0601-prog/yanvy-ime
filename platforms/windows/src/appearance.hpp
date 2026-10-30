// SPDX-License-Identifier: BSD-3-Clause
#pragma once
#include "common.hpp"
#include <algorithm>
#include <objidl.h>
#include <propidl.h>
#include <gdiplus.h>

namespace yanvy {
struct Appearance { int font_size=18, theme=0; };
inline Appearance ReadAppearance(const std::filesystem::path& root) {
  auto ini=root/L"settings.ini";
  return {std::clamp<int>(GetPrivateProfileIntW(L"display",L"font_size",18,ini.c_str()),12,32),
          std::clamp<int>(GetPrivateProfileIntW(L"display",L"theme",0,ini.c_str()),0,2)};
}
inline bool IsDark(Appearance appearance) {
  DWORD light=1,bytes=sizeof(light);
  RegGetValueW(HKEY_CURRENT_USER,L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
               L"AppsUseLightTheme",RRF_RT_REG_DWORD,nullptr,&light,&bytes);
  return appearance.theme==2 || (appearance.theme==0 && !light);
}
struct Palette {
  COLORREF background,border,text,muted,accent,selection,selection_text,badge_text,hover,line,chip;
  static Palette Get(bool dark) {
    if(dark) return {RGB(25,36,47),RGB(52,68,82),RGB(237,244,248),RGB(168,186,200),RGB(111,216,196),
        RGB(29,67,65),RGB(225,250,243),RGB(13,44,41),RGB(33,48,61),RGB(46,61,75),RGB(34,49,63)};
    return {RGB(255,255,255),RGB(222,231,237),RGB(25,45,62),RGB(94,112,129),RGB(12,119,112),
        RGB(230,246,240),RGB(12,76,68),RGB(255,255,255),RGB(241,247,248),RGB(232,238,242),RGB(241,246,248)};
  }
};
inline void Fill(HDC dc,RECT rect,COLORREF color) {
  auto brush=CreateSolidBrush(color);FillRect(dc,&rect,brush);DeleteObject(brush);
}
inline void Rounded(HDC dc,RECT rect,int radius,COLORREF fill,COLORREF border) {
  Gdiplus::Graphics graphics(dc);
  if(graphics.GetLastStatus()==Gdiplus::Ok){
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    const float x=float(rect.left)+.5f,y=float(rect.top)+.5f;
    const float width=float(rect.right-rect.left)-1.f,height=float(rect.bottom-rect.top)-1.f;
    const float r=std::min<float>(float(radius),std::min(width,height)/2.f),diameter=r*2.f;
    Gdiplus::GraphicsPath path;
    path.AddArc(x,y,diameter,diameter,180,90);path.AddArc(x+width-diameter,y,diameter,diameter,270,90);
    path.AddArc(x+width-diameter,y+height-diameter,diameter,diameter,0,90);path.AddArc(x,y+height-diameter,diameter,diameter,90,90);
    path.CloseFigure();
    Gdiplus::SolidBrush brush(Gdiplus::Color(255,GetRValue(fill),GetGValue(fill),GetBValue(fill)));
    Gdiplus::Pen pen(Gdiplus::Color(255,GetRValue(border),GetGValue(border),GetBValue(border)),1.f);
    graphics.FillPath(&brush,&path);graphics.DrawPath(&pen,&path);return;
  }
  auto brush=CreateSolidBrush(fill);auto pen=CreatePen(PS_SOLID,1,border);
  auto old_brush=SelectObject(dc,brush);auto old_pen=SelectObject(dc,pen);
  RoundRect(dc,rect.left,rect.top,rect.right,rect.bottom,radius*2,radius*2);
  SelectObject(dc,old_brush);SelectObject(dc,old_pen);DeleteObject(brush);DeleteObject(pen);
}
inline void Label(HDC dc,const wchar_t* text,RECT rect,HFONT font,COLORREF color,UINT flags=DT_LEFT) {
  auto old=SelectObject(dc,font);SetBkMode(dc,TRANSPARENT);SetTextColor(dc,color);
  DrawTextW(dc,text,-1,&rect,flags|DT_SINGLELINE|DT_VCENTER|DT_NOPREFIX|DT_END_ELLIPSIS);
  SelectObject(dc,old);
}
inline HFONT Font(int pixels,int weight=FW_NORMAL) {
  return CreateFontW(-pixels,0,0,0,weight,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,
      CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Microsoft YaHei UI");
}
}
