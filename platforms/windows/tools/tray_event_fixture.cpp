// SPDX-License-Identifier: BSD-3-Clause
#include "tray_icon.hpp"
#include <iostream>

int wmain() {
  using yanvy::TrayAction;
  using yanvy::TrayActionFor;
  const bool ok =
    TrayActionFor(true, 0, MAKELPARAM(WM_CONTEXTMENU, 1)) == TrayAction::Menu &&
    TrayActionFor(true, 0, MAKELPARAM(NIN_SELECT, 1)) == TrayAction::Settings &&
    TrayActionFor(true, 0, MAKELPARAM(NIN_KEYSELECT, 1)) == TrayAction::Settings &&
    TrayActionFor(true, 0, MAKELPARAM(WM_RBUTTONUP, 1)) == TrayAction::None &&
    TrayActionFor(true, 0, MAKELPARAM(WM_CONTEXTMENU, 2)) == TrayAction::None &&
    TrayActionFor(false, 1, WM_RBUTTONUP) == TrayAction::Menu &&
    TrayActionFor(false, 1, WM_LBUTTONUP) == TrayAction::Settings &&
    TrayActionFor(false, 2, WM_RBUTTONUP) == TrayAction::None;
  std::cout << (ok ? "tray event decoding PASS\n" : "tray event decoding FAIL\n");
  return ok ? 0 : 1;
}
