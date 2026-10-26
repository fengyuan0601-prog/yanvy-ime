// SPDX-License-Identifier: GPL-3.0-or-later
// Isolated fault injection for the production removal transaction. No TSF
// registration, registry access, process control or real user input.
#include "registration_flow.hpp"
#include <iostream>
#include <stdexcept>

namespace {
struct InstalledState {
  bool profile = true, categories = true, com = true, startup = true;
  int fail_at = 0;
  HRESULT Remove(bool& entry, int step) {
    if (fail_at == step) return E_ACCESSDENIED;
    entry = false;
    return S_OK;
  }
  HRESULT RemoveProfile() { return Remove(profile,1); }
  HRESULT RemoveCategories() { return Remove(categories,2); }
  HRESULT RemoveComServer() { return Remove(com,3); }
  HRESULT RemoveStartup() { return Remove(startup,4); }
};
void Require(bool value, const char* detail) {
  if (!value) throw std::runtime_error(detail);
}
}
int wmain() {
  try {
    for (int fail = 1; fail <= 4; ++fail) {
      InstalledState state; state.fail_at = fail;
      Require(yanvy::UnregisterTextService(state) == E_ACCESSDENIED,"removal error was hidden");
      Require(state.profile == (fail == 1),"profile changed after rejected removal");
      Require(state.categories == (fail <= 2),"categories removed after earlier failure");
      Require(state.com == (fail <= 3),"COM server removed after earlier failure");
      Require(state.startup,"startup removed after rejected removal");
      state.fail_at = 0;
      Require(yanvy::UnregisterTextService(state) == S_OK,"partial cleanup could not be retried");
      Require(!state.profile && !state.categories && !state.com && !state.startup,"retry left owned entries");
      Require(yanvy::UnregisterTextService(state) == S_OK,"already-absent cleanup failed");
    }
    std::cout << "registration cleanup: PASS (four failures, retry and absent-entry cases; no system mutation)\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
