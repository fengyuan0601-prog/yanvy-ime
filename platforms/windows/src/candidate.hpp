// SPDX-License-Identifier: BSD-3-Clause
#pragma once
#include "common.hpp"
#include <functional>
namespace yanvy {
class CandidateWindow {
 public:
  ~CandidateWindow();
  void Update(HINSTANCE module, const std::filesystem::path& root, const Response& state,
              bool pinyin, bool ascii, RECT caret, bool transient, std::function<void(int)> select);
  void Hide();
 private:
  static LRESULT CALLBACK Proc(HWND window, UINT message, WPARAM wp, LPARAM lp);
  void Paint();
  HWND window_ = nullptr;
  HINSTANCE module_ = nullptr;
  HFONT font_ = nullptr, small_ = nullptr;
  Response state_{};
  bool pinyin_ = false, ascii_ = false, dark_ = false;
  int pad_ = 12, row_ = 32, header_ = 38, preedit_ = 30, footer_ = 28;
  std::function<void(int)> select_;
};
}
