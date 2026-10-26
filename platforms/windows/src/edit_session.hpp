// SPDX-License-Identifier: GPL-3.0-or-later
// Adapted from rime/weasel 9cc96e20, WeaselTSF/EditSession.h.
// Original source and license: third_party/weasel/.
#pragma once
#include <windows.h>
#include <msctf.h>
#include <wrl/client.h>
#include <functional>
#include <new>

// Apartment-thread state shared by every edit for a service, including cleanup
// and positioning. Once a request is queued, later requests must also queue:
// TSF runs synchronous edits before pending asynchronous edits.
// https://learn.microsoft.com/en-us/windows/win32/api/msctf/nf-msctf-itfcontext-requesteditsession
struct EditSessionOrder {
  size_t pending = 0;
};

class EditSession final : public ITfEditSession {
 public:
  EditSession(IUnknown* service, ITfContext* context, EditSessionOrder& order,
      std::function<HRESULT(TfEditCookie)> fn)
      : service_(service), context_(context), fn_(std::move(fn)), order_(&order) { ++order_->pending; }
  ~EditSession() { Complete(); }
  bool Invoked() const { return invoked_; }
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
    if (invoked_) return E_UNEXPECTED;
    invoked_ = true;
    HRESULT result = E_FAIL;
    try { result = fn_(cookie); } catch (...) {}
    Complete();
    return result;
  }
 private:
  void Complete() {
    if (order_) { --order_->pending; order_ = nullptr; }
  }
  LONG refs_ = 1;
  Microsoft::WRL::ComPtr<IUnknown> service_;
  Microsoft::WRL::ComPtr<ITfContext> context_;
  std::function<HRESULT(TfEditCookie)> fn_;
  EditSessionOrder* order_;
  bool invoked_ = false;
};

inline HRESULT RequestOrderedEditSession(IUnknown* service, ITfContext* context,
    TfClientId client, EditSessionOrder& order, std::function<HRESULT(TfEditCookie)> function,
    bool write = true) {
  if (!service || !context) return E_INVALIDARG;
  const bool synchronous = order.pending == 0;
  auto edit = new(std::nothrow) EditSession(service,context,order,std::move(function));
  if (!edit) return E_OUTOFMEMORY;
  const DWORD access = write ? TF_ES_READWRITE : TF_ES_READ;
  HRESULT outcome = E_FAIL;
  HRESULT hr = context->RequestEditSession(client,edit,
      (synchronous ? TF_ES_SYNC : TF_ES_ASYNC) | access,&outcome);
  // A callback can itself fail after changing text. Never execute it again as
  // a fallback; only retry a request that TSF has not started.
  if (synchronous && !edit->Invoked() &&
      (FAILED(hr) || outcome == TF_E_SYNCHRONOUS || outcome == TS_E_NOLOCK)) {
    hr = context->RequestEditSession(client,edit,TF_ES_ASYNC | access,&outcome);
  }
  edit->Release();
  return FAILED(hr) ? hr : outcome;
}
