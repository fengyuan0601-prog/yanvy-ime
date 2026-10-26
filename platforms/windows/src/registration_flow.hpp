// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <windows.h>

namespace yanvy {
// Stop at the first failed removal. In particular a rejected TSF removal
// must leave the COM server and startup entry available to the installed IME.
// Operations must tolerate already-absent owned entries so a partial cleanup
// can be retried without treating absence as permission to ignore real errors.
template<class Operations> HRESULT UnregisterTextService(Operations& operations) {
  HRESULT hr = operations.RemoveProfile();
  if (FAILED(hr)) return hr;
  hr = operations.RemoveCategories();
  if (FAILED(hr)) return hr;
  hr = operations.RemoveComServer();
  if (FAILED(hr)) return hr;
  return operations.RemoveStartup();
}
}
