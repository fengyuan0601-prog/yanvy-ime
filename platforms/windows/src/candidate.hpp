// SPDX-License-Identifier: BSD-3-Clause
#pragma once
#include "common.hpp"
#include "candidate_view.hpp"
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
  void Place(UINT dpi);
  HWND window_ = nullptr;
  HINSTANCE module_ = nullptr;
  Response state_{};
  bool pinyin_ = false, ascii_ = false;
  int hover_ = -1;
  RECT caret_{};
  Appearance appearance_{};
  CandidateView view_;
  std::function<void(int)> select_;
};
}
