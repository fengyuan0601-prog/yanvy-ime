// SPDX-License-Identifier: BSD-3-Clause
#pragma once
#include <windows.h>
namespace yanvy {
// Never reuse a different document's caret when a query fails or is clipped.
inline void UpdateCaretExtent(RECT& caret,HRESULT result,const RECT& extent,BOOL clipped) {
  caret = {};
  if (SUCCEEDED(result) && !clipped && extent.right >= extent.left && extent.bottom > extent.top)
    caret = extent;
}
}
