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
class Document final : public ITfDocumentMgr {
 public:
  explicit Document(ITfContext* context):top(context){}
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid,void** out) override {
    if(!out)return E_POINTER;
    *out=nullptr;
    if(iid!=IID_IUnknown&&iid!=IID_ITfDocumentMgr)return E_NOINTERFACE;
    *out=static_cast<ITfDocumentMgr*>(this);AddRef();return S_OK;
  }
  ULONG STDMETHODCALLTYPE AddRef() override {return ++references_;}
  ULONG STDMETHODCALLTYPE Release() override {auto n=--references_;if(!n)delete this;return n;}
  HRESULT STDMETHODCALLTYPE CreateContext(TfClientId,DWORD,IUnknown*,ITfContext**,TfEditCookie*) override{return E_NOTIMPL;}
  HRESULT STDMETHODCALLTYPE Push(ITfContext*) override{return E_NOTIMPL;}
  HRESULT STDMETHODCALLTYPE Pop(DWORD) override{return E_NOTIMPL;}
  HRESULT STDMETHODCALLTYPE GetTop(ITfContext** out) override {if(!out)return E_POINTER;return top.CopyTo(out);}
  HRESULT STDMETHODCALLTYPE GetBase(ITfContext** out) override {return GetTop(out);}
  HRESULT STDMETHODCALLTYPE EnumContexts(IEnumTfContexts**) override{return E_NOTIMPL;}
  ComPtr<ITfContext> top;
 private:ULONG references_=1;
};

int wmain() {
  if (FAILED(CoCreateGuid(&yanvy::kToggleEnglish)) || FAILED(CoCreateGuid(&yanvy::kToggleEnglishRelease))) return 1;
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
        // SetKeyboardState affects this fixture thread's keyboard queue only;
        // it sends no desktop input and changes no system input profile.
        BYTE saved[256]{}, local[256]{};
        const bool keyboard_ready = GetKeyboardState(saved) && SetKeyboardState(local);
        auto chinese = [&]() { BOOL eaten=FALSE; return SUCCEEDED(service->OnTestKeyDown(context.Get(),VK_OEM_1,0,&eaten)) && eaten; };
        auto toggle = [&]() { BOOL eaten=FALSE; return SUCCEEDED(service->OnPreservedKey(context.Get(),yanvy::kToggleEnglish,&eaten)) && eaten; };
        if (keyboard_ready && set_open(true)) {
          service->BeginModeFixture(manager.Get(),client);
          // Service shortcut to English, OS change back to Chinese, repeated;
          // also verify the service callback can switch in both directions.
          bool ok=chinese() && set_open(false) && !chinese() && set_open(true) && chinese() &&
            toggle() && !chinese() && set_open(true) && chinese() &&
            toggle() && !chinese() && set_open(true) && chinese() &&
            toggle() && !chinese() && toggle() && chinese();
          local[VK_CONTROL]=local[VK_LCONTROL]=local[VK_SPACE]=0x80;
          BOOL eaten=FALSE;
          auto open_is = [&](bool expected) {
            VARIANT value; VariantInit(&value);
            const bool matches = SUCCEEDED(openclose->GetValue(&value)) &&
                value.vt==VT_I4 && (value.lVal!=0)==expected;
            VariantClear(&value); return matches;
          };
          ok = SetKeyboardState(local) && ok;
          ok = SUCCEEDED(service->OnKeyDown(context.Get(),VK_SPACE,0,&eaten)) && eaten && ok;
          ok = open_is(false) && ok;
          ok = SUCCEEDED(service->OnKeyDown(context.Get(),VK_SPACE,LPARAM(1) << 30,&eaten)) && eaten && ok;
          if (!open_is(false)) { std::cerr << "FAIL auto-repeat toggled the input mode twice\n"; ok=false; }
          service->OnKeyUp(context.Get(),VK_SPACE,0,&eaten);
          ok = SUCCEEDED(service->OnKeyDown(context.Get(),VK_SPACE,0,&eaten)) && eaten && open_is(true) && ok;
          ok = SUCCEEDED(service->OnKeyDown(context.Get(),VK_SPACE,LPARAM(1) << 30,&eaten)) && eaten && open_is(true) && ok;
          service->OnKeyUp(context.Get(),VK_SPACE,0,&eaten);
          local[VK_SHIFT]=local[VK_LSHIFT]=0x80;
          ok = SetKeyboardState(local) && ok;
          service->OnTestKeyDown(context.Get(),VK_SPACE,0,&eaten);
          if (eaten) { std::cerr << "FAIL Ctrl+Shift+Space was intercepted\n"; ok=false; }
          service->OnPreservedKey(context.Get(),yanvy::kToggleEnglish,&eaten);
          if (eaten || !open_is(true)) { std::cerr << "FAIL preserved Ctrl+Shift+Space was intercepted\n"; ok=false; }
          local[VK_SHIFT]=local[VK_LSHIFT]=0;
          ok = SetKeyboardState(local) && ok;
          ok = toggle() && open_is(false) && ok;
          for (int i=0;i<16;++i) ok = toggle() && open_is(false) && ok;
          // A host that delivers both paths must still switch only once.
          ok = SUCCEEDED(service->OnKeyDown(context.Get(),VK_SPACE,0,&eaten)) && eaten && open_is(false) && ok;
          local[VK_CONTROL]=local[VK_LCONTROL]=0; // release Ctrl before Space
          ok = SetKeyboardState(local) && ok;
          service->OnTestKeyUp(context.Get(),VK_CONTROL,0,&eaten);
          ok = !eaten && ok;
          local[VK_SPACE]=0;
          ok = SetKeyboardState(local) && ok;
          service->OnTestKeyUp(context.Get(),VK_SPACE,0,&eaten);
          ok = eaten && ok;
          service->OnKeyUp(context.Get(),VK_SPACE,0,&eaten);
          ok = eaten && ok;
          local[VK_CONTROL]=local[VK_LCONTROL]=local[VK_SPACE]=0x80;
          ok = SetKeyboardState(local) && ok;
          ok = toggle() && open_is(true) && ok;
          for (int i=0;i<16;++i) ok = toggle() && open_is(true) && ok;
          local[VK_SPACE]=0; // release Space before Ctrl
          ok = SetKeyboardState(local) && ok;
          service->OnTestKeyUp(context.Get(),VK_SPACE,0,&eaten); ok = eaten && ok;
          service->OnKeyUp(context.Get(),VK_SPACE,0,&eaten); ok = eaten && ok;
          local[VK_SPACE]=0x80; ok = SetKeyboardState(local) && ok;
          ok = SUCCEEDED(service->OnKeyDown(context.Get(),VK_SPACE,0,&eaten)) && eaten && open_is(false) && ok;
          for (int i=0;i<16;++i) ok = toggle() && open_is(false) && ok;
          service->OnKillThreadFocus(); // a release outside this app must not stick
          ok = toggle() && open_is(true) && ok;
          service->OnSetFocus(FALSE);
          ok = toggle() && open_is(true) && ok;
          service->OnKeyUp(context.Get(),VK_SPACE,0,&eaten);
          // Closed-keyboard hosts may deliver preserved releases only.
          ok = toggle() && open_is(false) && ok;
          service->OnPreservedKey(nullptr,yanvy::kToggleEnglishRelease,&eaten);
          ok = eaten && open_is(false) && ok;
          ok = toggle() && open_is(true) && ok;
          service->OnPreservedKey(nullptr,yanvy::kToggleEnglishRelease,&eaten);
          ok = eaten && open_is(true) && ok;
          service->OnPreservedKey(nullptr,yanvy::kToggleEnglishRelease,&eaten);
          ok = !eaten && open_is(true) && ok; // ordinary Space release passes on
          ComPtr<Document> document;document.Attach(new Document(context.Get()));
          service->SeedPendingFocusFixture(context.Get());
          service->OnSetFocus(document.Get(),document.Get());
          if(!service->HasPendingFocusFixture()){
            std::cerr<<"FAIL duplicate document focus discarded pending input\n";ok=false;
          }
          // A different top context must still cancel, even in the same doc.
          ComPtr<Context> other_context;other_context.Attach(new Context);
          document->top=other_context;
          service->SeedPendingFocusFixture(context.Get());
          service->OnSetFocus(document.Get(),document.Get());
          if(service->HasPendingFocusFixture()){
            std::cerr<<"FAIL changed document context retained pending input\n";ok=false;
          }
          service->SeedPendingFocusFixture(other_context.Get());
          service->OnSetFocus(nullptr,document.Get());
          if(service->HasPendingFocusFixture()){
            std::cerr<<"FAIL real document focus loss retained pending input\n";ok=false;
          }
          service->EndModeFixture(); result=ok ? 0 : 1;
        }
        if (keyboard_ready) SetKeyboardState(saved);
        service->Release();
      }
      manager->Deactivate();
    }
  }
  CoUninitialize();
  std::cout << (result==0 ? "TSF service and system mode round trips PASS\n" : "TSF service and system mode round trips FAIL\n");
  return result;
}
