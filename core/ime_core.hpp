// SPDX-License-Identifier: BSD-3-Clause
#pragma once
#include <rime_api.h>
#include <string>
#include <vector>

namespace ime {
struct Snapshot {
  bool handled = false;
  std::string schema;
  std::string input;
  std::string preedit;
  std::string commit;
  int page = 0;
  int highlighted = 0;
  int cursor = 0;
  bool last_page = true;
  bool ascii_mode = false;
  std::vector<std::string> candidates;
};

// A real librime adapter. It does not install or emulate a system keyboard.
// One instance per process: librime setup/finalize manages global state.
class Engine {
 public:
  Engine(const std::string& shared_dir, const std::string& user_dir);
  ~Engine();
  Engine(const Engine&) = delete;
  Engine& operator=(const Engine&) = delete;
  void SelectSchema(const std::string& id);
  Snapshot Key(int keycode, int modifiers = 0);
  Snapshot TypeAscii(const std::string& keys);
  Snapshot State();
  Snapshot Clear();
  const char* Version() const;
 private:
  void CheckPrivacy(const std::string& id);
  void CheckDefaultPolicy();
  RimeApi* api_ = nullptr;
  RimeSessionId session_ = 0;
  bool initialized_ = false;
  std::string shared_dir_, user_dir_;
};
}  // namespace ime
