// SPDX-License-Identifier: BSD-3-Clause
#include "tray_icon.hpp"
#include <iostream>

int wmain() {
  using yanvy::TrayAction;
  using yanvy::TrayActionFor;
  bool ok =
    TrayActionFor(true, 0, MAKELPARAM(WM_CONTEXTMENU, 1)) == TrayAction::Menu &&
    TrayActionFor(true, 0, MAKELPARAM(NIN_SELECT, 1)) == TrayAction::Settings &&
    TrayActionFor(true, 0, MAKELPARAM(NIN_KEYSELECT, 1)) == TrayAction::Settings &&
    TrayActionFor(true, 0, MAKELPARAM(WM_RBUTTONUP, 1)) == TrayAction::None &&
    TrayActionFor(true, 0, MAKELPARAM(WM_CONTEXTMENU, 2)) == TrayAction::None &&
    TrayActionFor(false, 1, WM_RBUTTONUP) == TrayAction::Menu &&
    TrayActionFor(false, 1, WM_LBUTTONUP) == TrayAction::Settings &&
    TrayActionFor(false, 2, WM_RBUTTONUP) == TrayAction::None;
  // A hidden-panel click belongs to its callback point, not a stale taskbar
  // icon rectangle or a cursor that moved while the callback was queued.
  const RECT taskbar_icon{3540,2080,3580,2120};
  const POINT moved_cursor{50,50};POINT anchor{};
  auto check=[&](bool result,const char* name){if(!result){std::cerr<<"FAIL "<<name<<'\n';ok=false;}};
  check(yanvy::TrayMenuAnchorFor(true,MAKEWPARAM(2600,1500),&taskbar_icon,&moved_cursor,anchor)&&
      anchor.x==2600&&anchor.y==1500,"hidden panel uses callback screen point");
  check(yanvy::TrayMenuAnchorFor(true,MAKEWPARAM(WORD(-1200),WORD(-90)),&taskbar_icon,&moved_cursor,anchor)&&
      anchor.x==-1200&&anchor.y==-90,"negative monitor coordinates stay signed");
  check(yanvy::TrayMenuAnchorFor(true,MAKEWPARAM(0,0),&taskbar_icon,&moved_cursor,anchor)&&
      anchor.x==0&&anchor.y==0,"screen origin is a valid callback point");
  check(yanvy::TrayMenuAnchorFor(true,MAKEWPARAM(WORD(-1),WORD(-1)),&taskbar_icon,&moved_cursor,anchor)&&
      anchor.x==3560&&anchor.y==2100,"keyboard invocation anchors to icon");
  check(yanvy::TrayMenuAnchorFor(true,MAKEWPARAM(WORD(-1),WORD(-1)),nullptr,&moved_cursor,anchor)&&
      anchor.x==50&&anchor.y==50,"missing keyboard icon falls back to cursor");
  const RECT negative_icon{-1480,-80,-1440,-40},empty_icon{};
  check(yanvy::TrayMenuAnchorFor(true,MAKEWPARAM(WORD(-1),WORD(-1)),&negative_icon,&moved_cursor,anchor)&&
      anchor.x==-1460&&anchor.y==-60,"keyboard anchor preserves negative icon coordinates");
  check(yanvy::TrayMenuAnchorFor(true,MAKEWPARAM(WORD(-1),WORD(-1)),&empty_icon,&moved_cursor,anchor)&&
      anchor.x==50&&anchor.y==50,"empty icon rectangle falls back to cursor");
  check(yanvy::TrayMenuAnchorFor(true,MAKEWPARAM(WORD(-1),90),nullptr,nullptr,anchor)&&
      anchor.x==-1&&anchor.y==90,"a single negative-one coordinate is not keyboard invocation");
  check(yanvy::TrayMenuAnchorFor(false,1,&taskbar_icon,&moved_cursor,anchor)&&
      anchor.x==50&&anchor.y==50,"legacy callback treats wParam as icon ID");
  check(!yanvy::TrayMenuAnchorFor(true,MAKEWPARAM(WORD(-1),WORD(-1)),nullptr,nullptr,anchor),"missing all anchors does not open at origin");
  // Exercise the actual Windows thread-DPI API without creating a window,
  // installing a tray icon, moving the cursor or opening a popup menu.
  const auto previous=SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_UNAWARE);
  check(previous!=nullptr,"fixture establishes unaware calling thread");
  if(previous){
    {
      yanvy::TrayDpiContext dpi;
      check(AreDpiAwarenessContextsEqual(GetThreadDpiAwarenessContext(),DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)!=FALSE,
          "tray thread opts into per-monitor-v2 coordinates");
    }
    check(AreDpiAwarenessContextsEqual(GetThreadDpiAwarenessContext(),DPI_AWARENESS_CONTEXT_UNAWARE)!=FALSE,
        "tray DPI context restores caller");
    SetThreadDpiAwarenessContext(previous);
  }
  std::cout << (ok ? "tray event, menu anchor and thread DPI checks PASS\n" : "tray event, menu anchor and thread DPI checks FAIL\n");
  return ok ? 0 : 1;
}
