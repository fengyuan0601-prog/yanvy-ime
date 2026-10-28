// SPDX-License-Identifier: BSD-3-Clause
#pragma once
#include <windows.h>
#include <msctf.h>
#include "msctf_compat.hpp"
#include <wrl/client.h>
#include <functional>
#include <utility>

namespace yanvy {
// Observes only this TSF thread manager; never changes other input methods.
class InputModeCompartment final : public ITfCompartmentEventSink {
 public:
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** out) override {
    if (!out) return E_POINTER;
    *out = nullptr;
    if (iid != IID_IUnknown && iid != IID_ITfCompartmentEventSink) return E_NOINTERFACE;
    *out = static_cast<ITfCompartmentEventSink*>(this); AddRef(); return S_OK;
  }
  ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&refs_); }
  ULONG STDMETHODCALLTYPE Release() override {
    ULONG count = InterlockedDecrement(&refs_); if (!count) delete this; return count;
  }
  HRESULT Attach(ITfThreadMgr* manager, TfClientId client, std::function<void(bool)> changed) {
    if (!manager || compartment_) return E_INVALIDARG;
    Microsoft::WRL::ComPtr<ITfCompartmentMgr> compartments;
    HRESULT hr = manager->QueryInterface(IID_PPV_ARGS(&compartments));
    if (SUCCEEDED(hr)) hr = compartments->GetCompartment(GUID_COMPARTMENT_KEYBOARD_OPENCLOSE, &compartment_);
    if (SUCCEEDED(hr)) hr = compartment_.As(&source_);
    client_ = client; changed_ = std::move(changed);
    if (SUCCEEDED(hr)) hr = source_->AdviseSink(IID_ITfCompartmentEventSink, this, &cookie_);
    if (FAILED(hr)) Detach();
    return hr;
  }
  void Detach() {
    if (source_ && cookie_ != TF_INVALID_COOKIE) source_->UnadviseSink(cookie_);
    cookie_ = TF_INVALID_COOKIE; changed_ = {}; source_.Reset(); compartment_.Reset();
  }
  bool Read(bool& open) const {
    if (!compartment_) return false;
    VARIANT value; VariantInit(&value);
    const HRESULT hr = compartment_->GetValue(&value);
    const bool valid = SUCCEEDED(hr) && value.vt == VT_I4;
    if (valid) open = value.lVal != 0;
    VariantClear(&value); return valid;
  }
  HRESULT SetOpen(bool open) {
    if (!compartment_) return E_FAIL;
    VARIANT value; VariantInit(&value); value.vt = VT_I4; value.lVal = open ? 1 : 0;
    return compartment_->SetValue(client_, &value);
  }
  HRESULT STDMETHODCALLTYPE OnChange(REFGUID guid) override {
    bool open = false;
    if (guid == GUID_COMPARTMENT_KEYBOARD_OPENCLOSE && Read(open) && changed_) changed_(open);
    return S_OK;
  }
 private:
  ~InputModeCompartment() { Detach(); }
  LONG refs_ = 1;
  TfClientId client_ = TF_CLIENTID_NULL;
  DWORD cookie_ = TF_INVALID_COOKIE;
  Microsoft::WRL::ComPtr<ITfCompartment> compartment_;
  Microsoft::WRL::ComPtr<ITfSource> source_;
  std::function<void(bool)> changed_;
};
}
