// SPDX-License-Identifier: GPL-3.0-or-later
// Exercises the real Service against a private TSF manager and a synthetic
// writable context. No installed TIP, input injection or broker is used.
#define YANVY_MODE_FIXTURE
#include "../src/tsf.cpp"
#include <iostream>

class Context final : public ITfContext {
 public:
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** out) override {
    if (!out) return E_POINTER;
    *out = nullptr;
    if (iid != IID_IUnknown && iid != IID_ITfContext) return E_NOINTERFACE;
    *out = static_cast<ITfContext*>(this); AddRef(); return S_OK;
  }
  ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&refs_); }
  ULONG STDMETHODCALLTYPE Release() override { auto count=InterlockedDecrement(&refs_); if (!count) delete this; return count; }
  HRESULT STDMETHODCALLTYPE GetStatus(TF_STATUS* status) override { if (!status) return E_POINTER; *status = {}; return S_OK; }
  HRESULT STDMETHODCALLTYPE RequestEditSession(TfClientId,ITfEditSession*,DWORD,HRESULT*) override { return E_NOTIMPL; }
  HRESULT STDMETHODCALLTYPE InWriteSession(TfClientId,BOOL*) override { return E_NOTIMPL; }
  HRESULT STDMETHODCALLTYPE GetSelection(TfEditCookie,ULONG,ULONG,TF_SELECTION*,ULONG*) override { return E_NOTIMPL; }
  HRESULT STDMETHODCALLTYPE SetSelection(TfEditCookie,ULONG,const TF_SELECTION*) override { return E_NOTIMPL; }
  HRESULT STDMETHODCALLTYPE GetStart(TfEditCookie,ITfRange**) override { return E_NOTIMPL; }
  HRESULT STDMETHODCALLTYPE GetEnd(TfEditCookie,ITfRange**) override { return E_NOTIMPL; }
  HRESULT STDMETHODCALLTYPE GetActiveView(ITfContextView**) override { return E_NOTIMPL; }
  HRESULT STDMETHODCALLTYPE EnumViews(IEnumTfContextViews**) override { return E_NOTIMPL; }
  HRESULT STDMETHODCALLTYPE GetProperty(REFGUID,ITfProperty**) override { return E_NOTIMPL; }
  HRESULT STDMETHODCALLTYPE GetAppProperty(REFGUID,ITfReadOnlyProperty**) override { return E_NOTIMPL; }
  HRESULT STDMETHODCALLTYPE TrackProperties(const GUID**,ULONG,const GUID**,ULONG,ITfReadOnlyProperty**) override { return E_NOTIMPL; }
  HRESULT STDMETHODCALLTYPE EnumProperties(IEnumTfProperties**) override { return E_NOTIMPL; }
  HRESULT STDMETHODCALLTYPE GetDocumentMgr(ITfDocumentMgr**) override { return E_NOTIMPL; }
  HRESULT STDMETHODCALLTYPE CreateRangeBackup(TfEditCookie,ITfRange*,ITfRangeBackup**) override { return E_NOTIMPL; }
 private: LONG refs_ = 1;
};

int wmain() {
  if (FAILED(CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED))) return 1;
  int result=1;
  {
    ComPtr<ITfThreadMgr> manager;
    TfClientId client=TF_CLIENTID_NULL;
    if (SUCCEEDED(CoCreateInstance(CLSID_TF_ThreadMgr,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&manager))) &&
        SUCCEEDED(manager->Activate(&client))) {
      ComPtr<ITfCompartmentMgr> compartments;
      ComPtr<ITfCompartment> openclose;
      if (SUCCEEDED(manager.As(&compartments)) &&
          SUCCEEDED(compartments->GetCompartment(GUID_COMPARTMENT_KEYBOARD_OPENCLOSE,&openclose))) {
        auto set_open = [&](bool open) {
          VARIANT value; VariantInit(&value); value.vt=VT_I4; value.lVal=open ? 1 : 0;
          return SUCCEEDED(openclose->SetValue(client,&value));
        };
        ComPtr<Context> context; context.Attach(new Context);
        auto service=new Service;
        auto chinese = [&]() { BOOL eaten=FALSE; return SUCCEEDED(service->OnTestKeyDown(context.Get(),VK_OEM_1,0,&eaten)) && eaten; };
        auto toggle = [&]() { BOOL eaten=FALSE; return SUCCEEDED(service->OnPreservedKey(context.Get(),yanvy::kToggleEnglish,&eaten)) && eaten; };
        if (set_open(true)) {
          service->BeginModeFixture(manager.Get(),client);
          // Service shortcut to English, OS change back to Chinese, repeated;
          // also verify the service callback can switch in both directions.
          const bool ok=chinese() && set_open(false) && !chinese() && set_open(true) && chinese() &&
            toggle() && !chinese() && set_open(true) && chinese() &&
            toggle() && !chinese() && set_open(true) && chinese() &&
            toggle() && !chinese() && toggle() && chinese();
          service->EndModeFixture(); result=ok ? 0 : 1;
        }
        service->Release();
      }
      manager->Deactivate();
    }
  }
  CoUninitialize();
  std::cout << (result==0 ? "TSF service and system mode round trips PASS\n" : "TSF service and system mode round trips FAIL\n");
  return result;
}
