// SPDX-License-Identifier: BSD-3-Clause
#pragma once
#include "appearance.hpp"
#include <limits>
namespace yanvy {
// One native painter is shared by the TSF popup and the clearly-labelled
// appearance preview. The preview does not create an input session.
class CandidateView {
 public:
  ~CandidateView();
  CandidateView()=default;
  CandidateView(const CandidateView&)=delete;
  CandidateView& operator=(const CandidateView&)=delete;
  void Set(const Response& state,uint32_t schema,bool ascii,Appearance appearance,UINT dpi,HICON icon=nullptr);
  SIZE Measure(HDC dc,int maximum_width,int maximum_height=std::numeric_limits<int>::max());
  void Draw(HDC dc,RECT bounds,int hover=-1);
  int HitTest(POINT point,RECT bounds) const;
  bool HitSchema(POINT point,RECT bounds) const;
  bool Dark() const { return dark_; }
 private:
  int D(int dip) const { return MulDiv(dip,int(dpi_),96); }
  int RowsTop() const;
  int RowHeight() const { return D(std::max(30,font_size_+14)); }
  int Height() const;
  void ClearFonts();
  void CreateFonts();
  HFONT primary_=nullptr,strong_=nullptr,caption_=nullptr,small_=nullptr;
  Response state_{};
  uint32_t schema_=1;
  bool ascii_=false,dark_=false,has_code_=false;
  UINT dpi_=0;
  int font_size_=0;
  HICON icon_=nullptr;
  ULONG_PTR graphics_=0;
};
}
