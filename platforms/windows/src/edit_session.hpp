// SPDX-License-Identifier: GPL-3.0-or-later
// Adapted from rime/weasel 9cc96e20, WeaselTSF/EditSession.h.
// Original source and license: third_party/weasel/.
#pragma once
#include <windows.h>
#include <msctf.h>
#include <wrl/client.h>
#include <functional>

class EditSession final : public ITfEditSession {
 public:
  EditSession(IUnknown* service, ITfContext* context, std::function<HRESULT(TfEditCookie)> fn)
      : service_(service), context_(context), fn_(std::move(fn)) {}
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** out) override {
    if (!out) return E_POINTER;
    *out = nullptr;
    if (iid == IID_IUnknown || iid == IID_ITfEditSession) *out = static_cast<ITfEditSession*>(this);
    if (!*out) return E_NOINTERFACE;
    AddRef();
    return S_OK;
  }
  ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&refs_); }
  ULONG STDMETHODCALLTYPE Release() override {
    ULONG count = InterlockedDecrement(&refs_);
    if (!count) delete this;
    return count;
  }
  HRESULT STDMETHODCALLTYPE DoEditSession(TfEditCookie cookie) override {
    try { return fn_(cookie); } catch (...) { return E_FAIL; }
  }
 private:
  LONG refs_ = 1;
  Microsoft::WRL::ComPtr<IUnknown> service_;
  Microsoft::WRL::ComPtr<ITfContext> context_;
  std::function<HRESULT(TfEditCookie)> fn_;
};
