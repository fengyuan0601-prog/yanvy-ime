// SPDX-License-Identifier: BSD-3-Clause
// Developer-only render of the actual native painter with synthetic text.
// This is not an IME input or application-compatibility test.
#include "candidate_view.hpp"
#include <cwchar>
#include <iostream>

// Exercise the actual painter geometry without opening a window or input session.
static int CheckLayout(){
  HDC dc=GetDC(nullptr);
  for(SIZE work:std::initializer_list<SIZE>{{640,480},{1366,728}})
  for(UINT dpi:{96u,144u,192u,288u})for(int font:{12,18,32})for(uint32_t count:{5u,10u}){
    yanvy::Response sample;sample.count=count;
    wcscpy_s(sample.preedit,L"ni hao");
    for(uint32_t i=0;i<count;++i)wcscpy_s(sample.candidates[i],L"你好");
    yanvy::CandidateView view;view.Set(sample,1,false,{font,1},dpi);
    SIZE size=view.Measure(dc,int(work.cx),int(work.cy));
    if(size.cx>work.cx||size.cy>work.cy){
      std::cerr<<"Candidate overflow: dpi="<<dpi<<" font="<<font<<" count="<<count<<" height="<<size.cy<<" available="<<work.cy<<"\n";
      ReleaseDC(nullptr,dc);return 1;
    }
    bool reachable[10]{};
    RECT bounds{0,0,size.cx,size.cy};
    for(int y=0;y<size.cy;++y){
      int hit=view.HitTest({size.cx/2,y},bounds);
      if(hit>=0&&hit<int(count))reachable[hit]=true;
    }
    for(uint32_t i=0;i<count;++i)if(!reachable[i]){ReleaseDC(nullptr,dc);return 2;}
    if(view.HitTest({size.cx/2,size.cy},bounds)!=-1){ReleaseDC(nullptr,dc);return 3;}
  }
  ReleaseDC(nullptr,dc);
  std::cout<<"48 candidate layouts: fit work area and every candidate remains clickable\n";return 0;
}

static int RenderBoundaries(const std::filesystem::path& output){
  if(std::filesystem::exists(output))return 3;
  yanvy::Response sample;sample.count=10;sample.last_page=0;
  wcscpy_s(sample.preedit,L"ni");
  const wchar_t* words[]={L"你",L"尼",L"呢",L"泥",L"拟",L"逆",L"倪",L"腻",L"妮",L"霓"};
  for(int i=0;i<10;++i)wcscpy_s(sample.candidates[i],words[i]);
  yanvy::CandidateView large,empty;
  large.Set(sample,1,false,{32,1},288);
  sample.count=0;wcscpy_s(sample.preedit,L"zzzzzz");
  empty.Set(sample,1,false,{32,2},192);
  HDC screen=GetDC(nullptr),dc=CreateCompatibleDC(screen);
  HBITMAP bitmap=CreateCompatibleBitmap(screen,1160,640);auto old=SelectObject(dc,bitmap);
  yanvy::Fill(dc,{0,0,1160,640},RGB(243,247,250));
  HFONT title=yanvy::Font(24,FW_SEMIBOLD),caption=yanvy::Font(16);
  yanvy::Label(dc,L"言舟 · 候选窗边界检查（原生绘制 / 合成示例）",{32,16,1128,56},title,RGB(22,44,62));
  yanvy::Label(dc,L"300% 缩放 / 32 号字 / 10 候选 / 可用高度 480",{32,62,568,100},caption,RGB(94,116,133));
  yanvy::Label(dc,L"无匹配候选：编码与提示分行显示",{620,62,1128,100},caption,RGB(94,116,133));
  auto size=large.Measure(dc,520,480);large.Draw(dc,{32,116,32+size.cx,116+size.cy});
  size=empty.Measure(dc,500,480);empty.Draw(dc,{620,116,620+size.cx,116+size.cy});
  DeleteObject(title);DeleteObject(caption);SelectObject(dc,old);DeleteDC(dc);ReleaseDC(nullptr,screen);
  int result=1;{Gdiplus::Bitmap png(bitmap,nullptr);CLSID encoder{0x557cf406,0x1a04,0x11d3,{0x9a,0x73,0x00,0x00,0xf8,0x1e,0xf3,0x2e}};
    if(png.Save(output.c_str(),&encoder,nullptr)==Gdiplus::Ok)result=0;}
  DeleteObject(bitmap);return result;
}

int wmain(int argc,wchar_t** argv){
  if(argc==2&&wcscmp(argv[1],L"--check-layout")==0)return CheckLayout();
  if(argc==3&&wcscmp(argv[1],L"--render-boundaries")==0)return RenderBoundaries(argv[2]);
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
