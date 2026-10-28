// SPDX-License-Identifier: BSD-3-Clause
#pragma once
#include <windows.h>
namespace yanvy {
// Numeric state only. This fixture API never returns keys, composition or text.
struct TextHostDiagnostics {
  LONG activations=0,deactivations=0,test_down=0,key_down=0,preserved=0;
  LONG cancellations=0,thread_blur=0,document_focus=0,selection_cancel=0,broker_focus=0;
};
}
