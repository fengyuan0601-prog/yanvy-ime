// SPDX-License-Identifier: BSD-3-Clause
#include "candidate_view.hpp"

namespace yanvy {
CandidateView::~CandidateView(){ClearFonts();if(graphics_)Gdiplus::GdiplusShutdown(graphics_);}
void CandidateView::ClearFonts(){
  for(auto font:{primary_,strong_,caption_,small_})if(font)DeleteObject(font);
  primary_=strong_=caption_=small_=nullptr;
}
void CandidateView::CreateFonts(){
  ClearFonts();
  primary_=Font(D(font_size_));strong_=Font(D(font_size_),FW_SEMIBOLD);
  caption_=Font(D(13),FW_MEDIUM);small_=Font(D(11));
}
void CandidateView::Set(const Response& state,uint32_t schema,bool ascii,Appearance appearance,UINT dpi,HICON icon){
  if(!graphics_){Gdiplus::GdiplusStartupInput options;Gdiplus::GdiplusStartup(&graphics_,&options,nullptr);}
  state_=state;SecureZeroMemory(state_.commit,sizeof(state_.commit));
  state_.count=std::min<uint32_t>(state_.count,10);
  schema_=schema<ime::kSchemaCount?schema:1;ascii_=ascii;dark_=IsDark(appearance);icon_=icon;
  has_code_=state.input[0]||state.preedit[0];
  int size=std::clamp(appearance.font_size,12,32);dpi=std::max<UINT>(dpi,96);
  if(dpi_!=dpi||font_size_!=size){
    dpi_=dpi;font_size_=size;CreateFonts();
  }
}
int CandidateView::RowsTop() const {
  return D(42)+(has_code_?D(font_size_+14):0)+D(5);
}
int CandidateView::Height() const {
  if(state_.count)return RowsTop()+int(state_.count)*RowHeight()+D(35);
  return has_code_?RowsTop()+D(38):D(100);
}
SIZE CandidateView::Measure(HDC dc,int maximum_width,int maximum_height){
  maximum_width=std::max(1,maximum_width);maximum_height=std::max(1,maximum_height);
  // Keep the whole page, footer and hit targets visible on a small work area.
  // A constrained popup uses a smaller rendering scale without changing the
  // saved font preference. The next Set restores the monitor's requested DPI.
  UINT requested=dpi_;
  while(dpi_>1&&(Height()>maximum_height||D(state_.count?264:302)>maximum_width))--dpi_;
  if(dpi_!=requested)CreateFonts();
  int width=D(state_.count?264:302);
  auto old=SelectObject(dc,strong_);
  for(uint32_t i=0;i<state_.count;++i){
    SIZE text{};GetTextExtentPoint32W(dc,state_.candidates[i],int(wcslen(state_.candidates[i])),&text);
    width=std::max<int>(width,text.cx+D(76));
  }
  if(has_code_){
    SIZE text{};const auto code=state_.preedit[0]?state_.preedit:state_.input;
    GetTextExtentPoint32W(dc,code,int(wcslen(code)),&text);width=std::max<int>(width,text.cx+D(32));
  }
  SelectObject(dc,old);
  width=std::min(width,std::min(D(540),maximum_width));
  return {width,std::min(Height(),maximum_height)};
}
void CandidateView::Draw(HDC dc,RECT bounds,int hover){
  const auto p=Palette::Get(dark_);const int left=bounds.left,top=bounds.top;
  Rounded(dc,bounds,D(11),p.background,p.border);
  int name_x=left+D(14);
  if(icon_){DrawIconEx(dc,name_x,top+D(14),icon_,D(16),D(16),0,nullptr,DI_NORMAL);name_x+=D(23);}
  Label(dc,L"言舟",{name_x,top+D(9),name_x+D(60),top+D(35)},caption_,p.text);
  RECT mode{bounds.right-D(40),top+D(11),bounds.right-D(12),top+D(33)};
  Rounded(dc,mode,D(6),ascii_?p.chip:p.selection,ascii_?p.chip:p.selection);
  Label(dc,ascii_?L"英":L"中",mode,small_,ascii_?p.muted:p.accent,DT_CENTER);
  RECT schema_button{bounds.right-D(122),mode.top,mode.left-D(8),mode.bottom};
  Rounded(dc,schema_button,D(5),p.chip,p.chip);
  auto schema_text=std::wstring(ime::kSchemaShortLabels[schema_])+L" ⇄";
  Label(dc,schema_text.c_str(),schema_button,small_,p.accent,DT_CENTER);
  if(has_code_){
    RECT code{left+D(15),top+D(39),bounds.right-D(15),top+RowsTop()-D(4)};
    Label(dc,state_.preedit[0]?state_.preedit:state_.input,code,primary_,p.accent);
  }
  int y=top+RowsTop();
  for(uint32_t i=0;i<state_.count;++i){
    const bool selected=i==state_.highlighted;
    RECT row{left+D(8),y,bounds.right-D(8),y+RowHeight()-D(2)};
    if(selected||int(i)==hover)Rounded(dc,row,D(7),selected?p.selection:p.hover,selected?p.selection:p.hover);
    RECT number{left+D(15),y+(RowHeight()-D(22))/2,left+D(37),y+(RowHeight()+D(22))/2};
    if(selected)Rounded(dc,number,D(5),p.accent,p.accent);
    auto index=std::to_wstring(i+1);
    Label(dc,index.c_str(),number,small_,selected?p.badge_text:p.muted,DT_CENTER);
    Label(dc,state_.candidates[i],{left+D(47),y,bounds.right-D(17),y+RowHeight()-D(2)},
          selected?strong_:primary_,selected?p.selection_text:p.text);
    y+=RowHeight();
  }
  if(state_.count){
    Fill(dc,{left+D(14),y+D(7),bounds.right-D(14),y+D(8)},p.line);
    auto page=L"第 "+std::to_wstring(state_.page+1)+L" 页";
    Label(dc,page.c_str(),{left+D(15),y+D(10),left+D(85),bounds.bottom-D(4)},small_,p.muted);
    Label(dc,(!state_.last_page||state_.page)?L"PgUp / PgDn 翻页":L"空格确认 · 数字选词",
          {left+D(80),y+D(10),bounds.right-D(15),bounds.bottom-D(4)},small_,p.muted,DT_RIGHT);
  }else if(has_code_){
    Label(dc,L"暂无候选 · Enter 提交编码",{left+D(15),y,bounds.right-D(15),bounds.bottom-D(6)},small_,p.muted);
  }else{
    Label(dc,L"Ctrl+Space  中英切换",{left+D(15),top+D(44),bounds.right-D(15),top+D(68)},small_,p.muted);
    Label(dc,L"Ctrl+Shift+F12  五笔 / 拼音",{left+D(15),top+D(67),bounds.right-D(15),bounds.bottom-D(9)},small_,p.muted);
  }
}
int CandidateView::HitTest(POINT point,RECT bounds) const {
  if(!PtInRect(&bounds,point))return -1;
  int y=point.y-bounds.top-RowsTop();
  if(point.x<bounds.left+D(8)||point.x>=bounds.right-D(8)||y<0)return -1;
  int index=y/RowHeight();return index<int(state_.count)?index:-1;
}
bool CandidateView::HitSchema(POINT point,RECT bounds) const {
  return point.x>=bounds.right-D(122)&&point.x<bounds.right-D(48)&&point.y>=bounds.top+D(11)&&point.y<bounds.top+D(33);
}
}
