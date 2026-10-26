// SPDX-License-Identifier: BSD-3-Clause
// Developer-only render of the actual native painter with synthetic text.
// This is not an IME input or application-compatibility test.
#include "candidate_view.hpp"
#include <cwchar>

int wmain(int argc,wchar_t** argv){
  if(argc!=3)return 2;
  auto output=std::filesystem::path(argv[1]);UINT dpi=UINT(std::clamp(_wtoi(argv[2]),96,288));
  if(std::filesystem::exists(output))return 3;
  auto D=[dpi](int value){return MulDiv(value,int(dpi),96);};
  yanvy::Response sample;sample.count=5;sample.last_page=0;
  wcscpy_s(sample.input,L"ni");wcscpy_s(sample.preedit,L"ni");
  const wchar_t* words[]={L"你",L"尼",L"呢",L"泥",L"拟"};
  for(int index=0;index<5;++index)wcscpy_s(sample.candidates[index],words[index]);
  yanvy::CandidateView light,dark;
  auto icon=static_cast<HICON>(LoadImageW(GetModuleHandleW(nullptr),MAKEINTRESOURCEW(1),IMAGE_ICON,D(16),D(16),LR_SHARED));
  light.Set(sample,true,false,{18,1},dpi,icon);dark.Set(sample,true,false,{18,2},dpi,icon);
  HDC screen=GetDC(nullptr),dc=CreateCompatibleDC(screen);
  HBITMAP bitmap=CreateCompatibleBitmap(screen,D(660),D(410));auto old=SelectObject(dc,bitmap);
  yanvy::Fill(dc,{0,0,D(660),D(410)},RGB(243,247,250));
  HFONT title=yanvy::Font(D(22),FW_SEMIBOLD),caption=yanvy::Font(D(12));
  yanvy::Label(dc,L"言舟 · 新版候选窗",{D(32),D(16),D(490),D(53)},title,RGB(22,44,62));
  yanvy::Label(dc,L"浅色 / 深色 · 合成示例",{D(32),D(54),D(620),D(79)},caption,RGB(94,116,133));
  auto size=light.Measure(dc,D(282));
  light.Draw(dc,{D(32),D(96),D(32)+size.cx,D(96)+size.cy});
  dark.Draw(dc,{D(364),D(96),D(364)+size.cx,D(96)+size.cy});
  DeleteObject(title);DeleteObject(caption);
  SelectObject(dc,old);DeleteDC(dc);ReleaseDC(nullptr,screen);
  int result=1;
  {
    Gdiplus::Bitmap image(bitmap,nullptr);
    CLSID png{0x557cf406,0x1a04,0x11d3,{0x9a,0x73,0x00,0x00,0xf8,0x1e,0xf3,0x2e}};
    if(image.Save(output.c_str(),&png,nullptr)==Gdiplus::Ok)result=0;
  }
  DeleteObject(bitmap);return result;
}
