// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <windows.h>
#include "ids.hpp"

namespace yanvy {
enum class RegistrationMode { fresh, repair };
// Repair runs only after the caller validates the identical protected runtime.
// Its writes repeat the fixed registration values; a failure must not remove
// a profile, category or COM server that the existing installation still uses.
template<class Operations> HRESULT RegisterTextService(Operations& operations,RegistrationMode mode) {
  HRESULT hr=operations.WriteComServer();
  if(FAILED(hr)){
    if(mode==RegistrationMode::fresh)operations.RemoveComServer();
    return hr;
  }
  hr=operations.RegisterProfile();
  if(SUCCEEDED(hr))hr=operations.RegisterCategories();
  if(FAILED(hr)&&mode==RegistrationMode::fresh){
    HRESULT cleanup=operations.RemoveProfile();
    if(SUCCEEDED(cleanup))cleanup=operations.RemoveRegisteredCategories();
    if(SUCCEEDED(cleanup))operations.RemoveComServer();
  }
  return hr;
}
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
// This allowlist contains only previous YANVY identities. The current identity
// and other installed input methods are never part of legacy retirement.
template<class Operations> HRESULT PreviousTextServicePresent(Operations& operations,bool& present) {
  present=false;
  for(const auto& identity:kPreviousTextServices){
    bool found=false;HRESULT hr=operations.Present(identity,found);if(FAILED(hr))return hr;
    if(found){present=true;return S_OK;}
  }
  return S_OK;
}
template<class Operations> HRESULT RetirePreviousTextServices(Operations& operations) {
  for(const auto& identity:kPreviousTextServices){HRESULT hr=operations.Remove(identity);if(FAILED(hr))return hr;}
  return S_OK;
}
}
