// SPDX-License-Identifier: BSD-3-Clause
#include "candidate.hpp"
#include <algorithm>
#include <windowsx.h>

namespace yanvy {
CandidateWindow::~CandidateWindow() {
  if (window_) DestroyWindow(window_);
  if (module_) UnregisterClassW(L"YANVY.Candidates.dev1", module_);
  if (font_) DeleteObject(font_);
  if (small_) DeleteObject(small_);
}
void CandidateWindow::Hide() { if (window_) ShowWindow(window_, SW_HIDE); }
void CandidateWindow::Update(HINSTANCE module, const std::filesystem::path& root, const Response& state,
    bool pinyin, bool ascii, RECT caret, bool transient, std::function<void(int)> select) {
  state_ = state;
  pinyin_ = pinyin;
  ascii_ = ascii;
  select_ = std::move(select);
  if (!transient && !state.input[0] && !state.preedit[0]) { Hide(); return; }
  if (!window_) {
    module_ = module;
    WNDCLASSW wc{};
    wc.hInstance = module;
    wc.lpfnWndProc = Proc;
    wc.lpszClassName = L"YANVY.Candidates.dev1";
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    RegisterClassW(&wc);
    auto old = SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    window_ = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        wc.lpszClassName, L"言舟候选", WS_POPUP | WS_BORDER, 0, 0, 360, 240,
        nullptr, nullptr, module, this);
    SetThreadDpiAwarenessContext(old);
  }
  if (!window_) return;
  const auto ini = root / L"settings.ini";
  int size = std::clamp<int>(GetPrivateProfileIntW(L"display", L"font_size", 18, ini.c_str()), 12, 32);
  int theme = GetPrivateProfileIntW(L"display", L"theme", 0, ini.c_str());
  DWORD light = 1, bytes = sizeof(light);
  RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
               L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &light, &bytes);
  dark_ = theme == 2 || (theme == 0 && !light);
  UINT dpi = 96;
  if (HWND foreground = GetForegroundWindow()) dpi = GetDpiForWindow(foreground);
  auto scale = [dpi](int v) { return MulDiv(v, dpi ? dpi : 96, 96); };
  pad_ = scale(12); row_ = scale(size + 16); header_ = scale(34); preedit_ = scale(size + 12); footer_ = scale(28);
  if (font_) DeleteObject(font_);
  if (small_) DeleteObject(small_);
  font_ = CreateFontW(-scale(size), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
      OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei UI");
  small_ = CreateFontW(-scale(12), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
      OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei UI");
  int width = scale(370), height = header_ + footer_ + (state_.input[0] ? preedit_ : 0) + int(state_.count) * row_;
  HDC dc = GetDC(window_);
  auto previous = SelectObject(dc, font_);
  for (uint32_t i = 0; i < state_.count; ++i) {
    SIZE extent{};
    GetTextExtentPoint32W(dc, state_.candidates[i], int(wcslen(state_.candidates[i])), &extent);
    width = std::max<int>(width, extent.cx + scale(76));
  }
  SelectObject(dc, previous);
  ReleaseDC(window_, dc);
  MONITORINFO mi{sizeof(mi)};
  GetMonitorInfoW(MonitorFromRect(&caret, MONITOR_DEFAULTTONEAREST), &mi);
  width = std::min(width, std::min(scale(680), int(mi.rcWork.right - mi.rcWork.left)));
  int x = std::clamp<int>(caret.left, mi.rcWork.left, mi.rcWork.right - width);
  int y = caret.bottom + scale(4);
  if (y + height > mi.rcWork.bottom) y = caret.top - height - scale(4);
  y = std::clamp<int>(y, mi.rcWork.top, std::max<int>(mi.rcWork.top, mi.rcWork.bottom - height));
  SetWindowPos(window_, HWND_TOPMOST, x, y, width, height, SWP_NOACTIVATE | SWP_SHOWWINDOW);
  InvalidateRect(window_, nullptr, TRUE);
  KillTimer(window_, 1);
  if (transient) SetTimer(window_, 1, 1400, nullptr);
}
void CandidateWindow::Paint() {
  PAINTSTRUCT ps{};
  HDC dc = BeginPaint(window_, &ps);
  RECT area{}; GetClientRect(window_, &area);
  const COLORREF background = dark_ ? RGB(27,34,42) : RGB(250,252,254);
  const COLORREF text = dark_ ? RGB(234,240,245) : RGB(23,41,58);
  HBRUSH brush = CreateSolidBrush(background);
  FillRect(dc, &area, brush); DeleteObject(brush);
  SetBkMode(dc, TRANSPARENT); SetTextColor(dc, text);
  SelectObject(dc, small_);
  std::wstring title = std::wstring(L"言舟 · ") + (pinyin_ ? L"拼音" : L"五笔86") + (ascii_ ? L"    英" : L"    中");
  RECT line{pad_,0,area.right-pad_,header_};
  DrawTextW(dc, title.c_str(), -1, &line, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
  int y = header_;
  SelectObject(dc, font_);
  if (state_.input[0]) {
    line = {pad_,y,area.right-pad_,y+preedit_};
    SetTextColor(dc, dark_ ? RGB(125,210,198) : RGB(18,117,122));
    DrawTextW(dc, state_.preedit[0] ? state_.preedit : state_.input, -1, &line,
              DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
    y += preedit_;
  }
  for (uint32_t i = 0; i < state_.count; ++i) {
    RECT row{pad_/2,y,area.right-pad_/2,y+row_};
    if (i == state_.highlighted) {
      brush = CreateSolidBrush(dark_ ? RGB(40,75,83) : RGB(218,241,239));
      FillRect(dc, &row, brush); DeleteObject(brush);
    }
    SetTextColor(dc, text);
    std::wstring value = std::to_wstring(i+1) + L"  " + state_.candidates[i];
    line = {pad_,y,area.right-pad_,y+row_};
    DrawTextW(dc, value.c_str(), -1, &line, DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
    y += row_;
  }
  SelectObject(dc, small_);
  SetTextColor(dc, dark_ ? RGB(164,178,189) : RGB(90,109,125));
  std::wstring hint = state_.count ? L"空格确认 · 数字选词 · PgUp/PgDn 翻页" : L"Ctrl+Space 中/英 · Ctrl+Shift+F12 五/拼";
  line = {pad_,y,area.right-pad_,y+footer_};
  DrawTextW(dc, hint.c_str(), -1, &line, DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
  EndPaint(window_, &ps);
}
LRESULT CALLBACK CandidateWindow::Proc(HWND window, UINT message, WPARAM wp, LPARAM lp) {
  auto self = reinterpret_cast<CandidateWindow*>(GetWindowLongPtrW(window, GWLP_USERDATA));
  if (message == WM_NCCREATE) {
    self = static_cast<CandidateWindow*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
    SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
  }
  if (self) {
    if (message == WM_MOUSEACTIVATE) return MA_NOACTIVATE;
    if (message == WM_PAINT) { self->Paint(); return 0; }
    if (message == WM_TIMER) { self->Hide(); return 0; }
    if (message == WM_LBUTTONUP) {
      const int offset = self->header_ + (self->state_.input[0] ? self->preedit_ : 0);
      const int y = GET_Y_LPARAM(lp) - offset;
      if (y >= 0 && y / self->row_ < int(self->state_.count) && self->select_)
        self->select_(y / self->row_);
      return 0;
    }
  }
  return DefWindowProcW(window, message, wp, lp);
}
}
