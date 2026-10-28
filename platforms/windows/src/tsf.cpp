// SPDX-License-Identifier: GPL-3.0-or-later
// TSF composition, edit-session, sink and key handling adapted from
// rime/weasel 9cc96e20dc71b80876b12f689bb5863c76c2a7ed (GPL-3.0-or-later).
// See third_party/weasel/PROVENANCE.md for original files and changes.
#include "common.hpp"
#include "ids.hpp"
#include "candidate.hpp"
#include "caret_geometry.hpp"
#include "edit_session.hpp"
#include "input_mode.hpp"
#include "msctf_compat.hpp"
#include <wrl/client.h>
#include <atomic>
#include <algorithm>
#include <new>

using Microsoft::WRL::ComPtr;
namespace {
HINSTANCE module = nullptr;
std::atomic<long> objects{0};

class Service final : public ITfTextInputProcessorEx, public ITfKeyEventSink,
    public ITfThreadMgrEventSink, public ITfThreadFocusSink,
    public ITfCompositionSink, public ITfTextEditSink, public ITfTextLayoutSink {
 public:
  Service() { ++objects; CoCreateGuid(&owner_); }
  ~Service() { Deactivate(); --objects; }
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** out) override {
    if (!out) return E_POINTER;
    *out = nullptr;
    if (iid == IID_IUnknown || iid == IID_ITfTextInputProcessor || iid == IID_ITfTextInputProcessorEx)
      *out = static_cast<ITfTextInputProcessorEx*>(this);
    else if (iid == IID_ITfKeyEventSink) *out = static_cast<ITfKeyEventSink*>(this);
    else if (iid == IID_ITfThreadMgrEventSink) *out = static_cast<ITfThreadMgrEventSink*>(this);
    else if (iid == IID_ITfThreadFocusSink) *out = static_cast<ITfThreadFocusSink*>(this);
    else if (iid == IID_ITfCompositionSink) *out = static_cast<ITfCompositionSink*>(this);
    else if (iid == IID_ITfTextEditSink) *out = static_cast<ITfTextEditSink*>(this);
    else if (iid == IID_ITfTextLayoutSink) *out = static_cast<ITfTextLayoutSink*>(this);
    if (!*out) return E_NOINTERFACE;
    AddRef(); return S_OK;
  }
  ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&refs_); }
  ULONG STDMETHODCALLTYPE Release() override {
    const ULONG count = InterlockedDecrement(&refs_);
    if (!count) delete this;
    return count;
  }
  HRESULT STDMETHODCALLTYPE Activate(ITfThreadMgr* manager, TfClientId id) override { return ActivateEx(manager,id,0); }
  HRESULT STDMETHODCALLTYPE ActivateEx(ITfThreadMgr* manager, TfClientId id, DWORD) override {
    if (!manager) return E_INVALIDARG;
    if (manager_) return E_UNEXPECTED;
    manager_ = manager; client_ = id;
    auto preferences=yanvy::PreferenceRoot(yanvy::ModuleDir(module));
    schema_=preferred_schema_=yanvy::ReadSchemaPreference(preferences);
    preference_revision_=yanvy::ReadInputRevision(preferences);
    last_pinyin_schema_=schema_?schema_:yanvy::ReadPinyinSchemaPreference(preferences);
    ComPtr<ITfKeystrokeMgr> keys;
    ComPtr<ITfSource> source;
    HRESULT hr = manager_->QueryInterface(IID_PPV_ARGS(&keys));
    if (SUCCEEDED(hr)) hr = keys->AdviseKeyEventSink(client_, static_cast<ITfKeyEventSink*>(this), TRUE);
    if (SUCCEEDED(hr)) {
      TF_PRESERVEDKEY shortcut{VK_SPACE,TF_MOD_CONTROL};
      const wchar_t description[]=L"言舟中英文切换";
      preserved_ = SUCCEEDED(keys->PreserveKey(client_,yanvy::kToggleEnglish,&shortcut,description,UINT(wcslen(description))));
    }
    if (SUCCEEDED(hr)) hr = manager_->QueryInterface(IID_PPV_ARGS(&source));
    if (SUCCEEDED(hr)) hr = source->AdviseSink(IID_ITfThreadMgrEventSink, static_cast<ITfThreadMgrEventSink*>(this), &thread_cookie_);
    if (SUCCEEDED(hr)) source->AdviseSink(IID_ITfThreadFocusSink, static_cast<ITfThreadFocusSink*>(this), &focus_cookie_);
    if (FAILED(hr)) { Deactivate(); return hr; }
    AttachInputMode();
    ComPtr<ITfDocumentMgr> document;
    if (SUCCEEDED(manager_->GetFocus(&document))) Bind(document.Get());
    yanvy::StartBroker(yanvy::ModuleDir(module));
    return S_OK;
  }
  HRESULT STDMETHODCALLTYPE Deactivate() override {
    if (!manager_) return S_OK;
    if (mode_) { mode_->Detach(); mode_.Reset(); }
    Cancel(); Bind(nullptr);
    ComPtr<ITfSource> source;
    if (SUCCEEDED(manager_->QueryInterface(IID_PPV_ARGS(&source)))) {
      if (thread_cookie_ != TF_INVALID_COOKIE) source->UnadviseSink(thread_cookie_);
      if (focus_cookie_ != TF_INVALID_COOKIE) source->UnadviseSink(focus_cookie_);
    }
    thread_cookie_ = focus_cookie_ = TF_INVALID_COOKIE;
    ComPtr<ITfKeystrokeMgr> keys;
    if (SUCCEEDED(manager_->QueryInterface(IID_PPV_ARGS(&keys)))) {
      if (preserved_) { TF_PRESERVEDKEY shortcut{VK_SPACE,TF_MOD_CONTROL};keys->UnpreserveKey(yanvy::kToggleEnglish,&shortcut); }
      keys->UnadviseKeyEventSink(client_);
    }
    preserved_=false;
    manager_.Reset(); active_ = false;
    return S_OK;
  }
  HRESULT STDMETHODCALLTYPE OnSetFocus(BOOL foreground) override {
    if (!foreground) Cancel();
    return S_OK;
  }
  HRESULT STDMETHODCALLTYPE OnSetThreadFocus() override { RefreshPreference();return S_OK; }
  HRESULT STDMETHODCALLTYPE OnKillThreadFocus() override { Cancel(); return S_OK; }
  HRESULT STDMETHODCALLTYPE OnInitDocumentMgr(ITfDocumentMgr*) override { return S_OK; }
  HRESULT STDMETHODCALLTYPE OnUninitDocumentMgr(ITfDocumentMgr*) override { return S_OK; }
  HRESULT STDMETHODCALLTYPE OnPushContext(ITfContext*) override { Cancel(); return S_OK; }
  HRESULT STDMETHODCALLTYPE OnPopContext(ITfContext*) override { Cancel(); return S_OK; }
  HRESULT STDMETHODCALLTYPE OnSetFocus(ITfDocumentMgr* focus, ITfDocumentMgr*) override {
    Cancel(); Bind(focus);RefreshPreference();return S_OK;
  }
  HRESULT STDMETHODCALLTYPE OnTestKeyDown(ITfContext* context, WPARAM key, LPARAM, BOOL* eaten) override {
    if (!eaten) return E_POINTER;
    *eaten = Interested(context,key); return S_OK;
  }
  HRESULT STDMETHODCALLTYPE OnKeyDown(ITfContext* context, WPARAM key, LPARAM lp, BOOL* eaten) override {
    if (!eaten) return E_POINTER;
    *eaten = FALSE;
    if (!Interested(context,key)) return S_OK;
    try {
      RefreshPreference();
      if (context_.Get() != context) { Cancel(); BindContext(context); }
      if (key == VK_SPACE && Down(VK_CONTROL)) {
        ToggleInputMode(); *eaten = TRUE; return S_OK;
      }
      if (key == VK_F12 && Down(VK_CONTROL) && Down(VK_SHIFT)) {
        Cancel();if(schema_){last_pinyin_schema_=schema_;schema_=0;}else schema_=last_pinyin_schema_;
        Show(true); *eaten = TRUE; return S_OK;
      }
      yanvy::Response state;
      if (!EnsureFocus()) return S_OK;
      auto q = Request(yanvy::Op::Key);
      q.key = Translate(key, lp, q.modifiers);
      if (!q.key || !yanvy::Call(q,state)) { Cancel(); return S_OK; }
      if (!Apply(context,state)) { Cancel(); return S_OK; }
      // An unhandled key can still finish a composition. Apply that edit
      // before passing the original key on to the application.
      *eaten = state.handled;
    } catch (...) { Cancel(); }
    return S_OK;
  }
  HRESULT STDMETHODCALLTYPE OnTestKeyUp(ITfContext*, WPARAM, LPARAM, BOOL* eaten) override {
    if (!eaten) return E_POINTER;
    *eaten = FALSE; return S_OK;
  }
  HRESULT STDMETHODCALLTYPE OnKeyUp(ITfContext*, WPARAM, LPARAM, BOOL* eaten) override {
    if (!eaten) return E_POINTER;
    *eaten = FALSE; return S_OK;
  }
  HRESULT STDMETHODCALLTYPE OnPreservedKey(ITfContext* context, REFGUID key, BOOL* eaten) override {
    if (!eaten) return E_POINTER;
    *eaten = FALSE;
    if (key == yanvy::kToggleEnglish && manager_ && !Disabled(context)) {
      ToggleInputMode();*eaten=TRUE;
    }
    return S_OK;
  }
  HRESULT STDMETHODCALLTYPE OnCompositionTerminated(TfEditCookie ec, ITfComposition* composition) override {
    if (composition_.Get() == composition) {
      auto terminated = composition_;
      composition_.Reset();
      ComPtr<ITfRange> range;
      if (SUCCEEDED(terminated->GetRange(&range))) range->SetText(ec, 0, L"", 0);
      ++generation_; ClearEngine(); last_ = {}; candidates_.Hide();
    }
    return S_OK;
  }
  HRESULT STDMETHODCALLTYPE OnEndEdit(ITfContext* context, TfEditCookie ec, ITfEditRecord* record) override {
    if (!composition_ || applying_ || context != context_.Get()) return S_OK;
    BOOL changed = FALSE;
    if (SUCCEEDED(record->GetSelectionStatus(&changed)) && changed) {
      TF_SELECTION selection{}; ULONG fetched = 0;
      ComPtr<ITfRange> range;
      if (SUCCEEDED(context->GetSelection(ec,TF_DEFAULT_SELECTION,1,&selection,&fetched)) && fetched) {
        LONG start=0,end=0;
        bool covered = SUCCEEDED(composition_->GetRange(&range)) &&
            SUCCEEDED(range->CompareStart(ec,selection.range,TF_ANCHOR_START,&start)) && start <= 0 &&
            SUCCEEDED(range->CompareEnd(ec,selection.range,TF_ANCHOR_END,&end)) && end >= 0;
        selection.range->Release();
        if (!covered) Cancel();
      }
    }
    return S_OK;
  }
  HRESULT STDMETHODCALLTYPE OnLayoutChange(ITfContext* context, TfLayoutCode code, ITfContextView*) override {
    if (code == TF_LC_DESTROY) Cancel();
    else if (code == TF_LC_CHANGE && composition_ && context == context_.Get()) Position();
    return S_OK;
  }
#ifdef YANVY_MODE_FIXTURE
  void BeginModeFixture(ITfThreadMgr* manager, TfClientId client) {
    manager_ = manager; client_ = client; AttachInputMode();
  }
  void EndModeFixture() {
    if (mode_) { mode_->Detach(); mode_.Reset(); }
    manager_.Reset();
  }
#endif
 private:
  void AttachInputMode() {
    mode_.Attach(new(std::nothrow) yanvy::InputModeCompartment);
    if (mode_ && SUCCEEDED(mode_->Attach(manager_.Get(),client_,[this](bool open) {
      if (ascii_ != !open) { Cancel(); ascii_ = !open; Show(true); }
    }))) {
      bool open = true; if (mode_->Read(open)) ascii_ = !open;
    } else mode_.Reset();
  }
  void ToggleInputMode() {
    const bool next_ascii = !ascii_;
    Cancel(); ascii_ = next_ascii;
    if (mode_) mode_->SetOpen(!next_ascii);
    Show(true);
  }
  static bool Down(int vk) { return (GetKeyState(vk) & 0x8000) != 0; }
  bool Disabled(ITfContext* context) {
    if (!context) return true;
    TF_STATUS status{};
    if (FAILED(context->GetStatus(&status)) || (status.dwDynamicFlags & TF_SD_READONLY)) return true;
    ComPtr<ITfCompartmentMgr> compartments;
    if (SUCCEEDED(context->QueryInterface(IID_PPV_ARGS(&compartments)))) {
      for (const GUID* guid : {&GUID_COMPARTMENT_KEYBOARD_DISABLED, &GUID_COMPARTMENT_EMPTYCONTEXT}) {
        ComPtr<ITfCompartment> compartment;
        VARIANT value; VariantInit(&value);
        if (SUCCEEDED(compartments->GetCompartment(*guid,&compartment)) &&
            SUCCEEDED(compartment->GetValue(&value)) && value.vt == VT_I4 && value.lVal) {
          VariantClear(&value); return true;
        }
        VariantClear(&value);
      }
    }
    return false;
  }
  bool Interested(ITfContext* context, WPARAM key) {
    if (!manager_ || Disabled(context)) return false;
    if (Down(VK_LWIN) || Down(VK_RWIN) || Down(VK_MENU)) return false;
    if (key == VK_SPACE && Down(VK_CONTROL)) return true;
    if (key == VK_F12 && Down(VK_CONTROL) && Down(VK_SHIFT)) return true;
    if (Down(VK_CONTROL) || ascii_) return false;
    if (key >= 'A' && key <= 'Z') return !(GetKeyState(VK_CAPITAL) & 1);
    if (key >= VK_OEM_1 && key <= VK_OEM_8) return true;
    const bool pending = last_.input[0] || last_.preedit[0];
    if (!pending) return false;
    return (key >= '0' && key <= '9') || key == VK_SPACE || key == VK_RETURN ||
        key == VK_BACK || key == VK_DELETE || key == VK_ESCAPE || key == VK_LEFT || key == VK_RIGHT ||
        key == VK_HOME || key == VK_END || key == VK_UP || key == VK_DOWN || key == VK_PRIOR || key == VK_NEXT;
  }
  // Virtual-key translation follows WeaselTSF/KeyEvent.cpp. Dead-key state is
  // left unchanged (ToUnicodeEx flag 4 on supported Windows versions).
  uint32_t Translate(WPARAM key, LPARAM lp, uint32_t& modifiers) {
    modifiers = Down(VK_SHIFT) ? 1u : 0u;
    if (GetKeyState(VK_CAPITAL) & 1) modifiers |= 2;
    switch (key) {
      case VK_BACK: return 0xff08;
      case VK_RETURN: return 0xff0d;
      case VK_ESCAPE: return 0xff1b;
      case VK_DELETE: return 0xffff;
      case VK_LEFT: return 0xff51;
      case VK_UP: return 0xff52;
      case VK_RIGHT: return 0xff53;
      case VK_DOWN: return 0xff54;
      case VK_HOME: return 0xff50;
      case VK_END: return 0xff57;
      case VK_PRIOR: return 0xff55;
      case VK_NEXT: return 0xff56;
      case VK_SPACE: return ' ';
    }
    BYTE keyboard[256]{}; WCHAR text[8]{};
    if (!GetKeyboardState(keyboard)) return 0;
    keyboard[VK_CONTROL] = keyboard[VK_MENU] = 0;
    int n = ToUnicodeEx(UINT(key),UINT((lp >> 16) & 0xff),keyboard,text,8,4,GetKeyboardLayout(0));
    return n == 1 ? uint32_t(text[0]) : 0;
  }
  yanvy::Request Request(yanvy::Op op) const {
    yanvy::Request q; q.owner = owner_; q.op = op; q.schema = schema_; return q;
  }
  void RefreshPreference(){
    auto preferences=yanvy::PreferenceRoot(yanvy::ModuleDir(module));
    auto preference=yanvy::ReadSchemaPreference(preferences);
    auto revision=yanvy::ReadInputRevision(preferences);
    last_pinyin_schema_=yanvy::ReadPinyinSchemaPreference(preferences);
    if(preference!=preferred_schema_||revision!=preference_revision_){Cancel();schema_=preferred_schema_=preference;preference_revision_=std::move(revision);if(schema_)last_pinyin_schema_=schema_;}
  }
  bool EnsureFocus() {
    if (active_) return true;
    yanvy::Response r;
    active_ = yanvy::Call(Request(yanvy::Op::Focus),r);
    if (!active_) yanvy::StartBroker(yanvy::ModuleDir(module));
    return active_;
  }
  void ClearEngine() {
    if (active_) { yanvy::Response r; yanvy::Call(Request(yanvy::Op::Clear),r); }
    active_ = false;
  }
  HRESULT Edit(ITfContext* context, std::function<HRESULT(TfEditCookie)> function, bool write=true) {
    if (!context || !manager_) return E_FAIL;
    return RequestOrderedEditSession(static_cast<ITfTextInputProcessorEx*>(this),context,
        client_,edit_order_,std::move(function),write);
  }
  void Cancel() {
    ++generation_; ClearEngine(); last_ = {}; candidates_.Hide();
    ComPtr<ITfComposition> old = composition_;
    ComPtr<ITfContext> context = context_;
    composition_.Reset();
    if (old && context) {
      Edit(context.Get(),[old,context](TfEditCookie ec) {
        ComPtr<ITfRange> range;
        if (SUCCEEDED(old->GetRange(&range))) range->SetText(ec,0,L"",0);
        return old->EndComposition(ec);
      });
    }
  }
  void Bind(ITfDocumentMgr* document) {
    ComPtr<ITfContext> context;
    if (document) document->GetTop(&context);
    BindContext(context.Get());
  }
  void BindContext(ITfContext* context) {
    if (context_.Get() == context) return;
    caret_ = {};
    ComPtr<ITfSource> source;
    if (context_ && SUCCEEDED(context_->QueryInterface(IID_PPV_ARGS(&source)))) {
      if (edit_cookie_ != TF_INVALID_COOKIE) source->UnadviseSink(edit_cookie_);
      if (layout_cookie_ != TF_INVALID_COOKIE) source->UnadviseSink(layout_cookie_);
    }
    edit_cookie_ = layout_cookie_ = TF_INVALID_COOKIE;
    context_ = context;
    CoCreateGuid(&owner_); active_ = false;
    source.Reset();
    if (context_ && SUCCEEDED(context_->QueryInterface(IID_PPV_ARGS(&source)))) {
      source->AdviseSink(IID_ITfTextEditSink,static_cast<ITfTextEditSink*>(this),&edit_cookie_);
      source->AdviseSink(IID_ITfTextLayoutSink,static_cast<ITfTextLayoutSink*>(this),&layout_cookie_);
    }
  }
  HRESULT StartComposition(ITfContext* context, TfEditCookie ec) {
    ComPtr<ITfInsertAtSelection> insert;
    ComPtr<ITfContextComposition> compositions;
    ComPtr<ITfRange> range;
    HRESULT hr = context->QueryInterface(IID_PPV_ARGS(&insert));
    if (SUCCEEDED(hr)) hr = insert->InsertTextAtSelection(ec,TF_IAS_QUERYONLY,nullptr,0,&range);
    if (SUCCEEDED(hr)) hr = context->QueryInterface(IID_PPV_ARGS(&compositions));
    if (SUCCEEDED(hr)) hr = compositions->StartComposition(ec,range.Get(),this,&composition_);
    return hr;
  }
  HRESULT SetText(ITfContext* context, TfEditCookie ec, const wchar_t* text, int cursor=-1) {
    ComPtr<ITfRange> range;
    HRESULT hr = composition_ ? composition_->GetRange(&range) : E_FAIL;
    if (FAILED(hr)) return hr;
    hr = range->SetText(ec,0,text,LONG(wcslen(text)));
    if (FAILED(hr)) return hr;
    range->Collapse(ec,cursor < 0 ? TF_ANCHOR_END : TF_ANCHOR_START);
    if (cursor >= 0) { LONG moved=0; range->ShiftStart(ec,std::min<int>(cursor,int(wcslen(text))),&moved,nullptr); range->Collapse(ec,TF_ANCHOR_START); }
    TF_SELECTION selection{range.Get(),{TF_AE_NONE,FALSE}};
    return context->SetSelection(ec,1,&selection);
  }
  bool Apply(ITfContext* context, yanvy::Response state) {
    const auto generation = generation_;
    const auto revision = ++snapshot_revision_;
    // Keystroke decisions follow the newest accepted engine response. An
    // older queued document edit must not roll this snapshot backwards.
    last_ = state;
    SecureZeroMemory(last_.commit,sizeof(last_.commit));
    ComPtr<ITfContext> target(context);
    HRESULT hr = Edit(context,[this,target,state,generation,revision](TfEditCookie ec) {
      if (generation != generation_ || target.Get() != context_.Get()) return S_OK;
      applying_ = true;
      struct Reset { bool& flag; ~Reset(){ flag=false; } } reset{applying_};
      HRESULT result = S_OK;
      if (state.commit[0]) {
        if (!composition_) result = StartComposition(target.Get(),ec);
        if (SUCCEEDED(result)) result = SetText(target.Get(),ec,state.commit);
        if (FAILED(result)) return result;
        auto finished = composition_; composition_.Reset();
        result = finished->EndComposition(ec);
        if (FAILED(result)) return result;
      }
      if (state.preedit[0] || state.input[0]) {
        if (!composition_) result = StartComposition(target.Get(),ec);
        if (SUCCEEDED(result)) result = SetText(target.Get(),ec,state.preedit[0] ? state.preedit : state.input,int(state.cursor));
      } else if (composition_) {
        auto finished = composition_;
        ComPtr<ITfRange> range;
        if (SUCCEEDED(finished->GetRange(&range))) range->SetText(ec,0,L"",0);
        composition_.Reset(); result = finished->EndComposition(ec);
      }
      if (SUCCEEDED(result) && revision == snapshot_revision_) Position(ec);
      return result;
    });
    return SUCCEEDED(hr);
  }
  void Position(TfEditCookie ec) {
    if (!context_) return;
    caret_ = {};
    ComPtr<ITfContextView> view;
    ComPtr<ITfRange> range;
    if (FAILED(context_->GetActiveView(&view))) { candidates_.Hide(); return; }
    if (composition_) composition_->GetRange(&range);
    if (!range) {
      TF_SELECTION selection{}; ULONG fetched=0;
      if (SUCCEEDED(context_->GetSelection(ec,TF_DEFAULT_SELECTION,1,&selection,&fetched)) && fetched) range.Attach(selection.range);
    }
    if (range) {
      range->Collapse(ec,TF_ANCHOR_START);
      RECT caret{}; BOOL clipped=FALSE;
      HRESULT result=view->GetTextExt(ec,range.Get(),&caret,&clipped);
      yanvy::UpdateCaretExtent(caret_,result,caret,clipped);
    }
    Show(false);
  }
  void Position() {
    ComPtr<ITfContext> target = context_;
    const auto generation = generation_;
    if (target) Edit(target.Get(),[this,target,generation](TfEditCookie ec) {
      if (generation == generation_ && target.Get() == context_.Get()) Position(ec);
      return S_OK;
    },false);
  }
  void Show(bool transient) {
    if (!caret_.bottom) {
      GUITHREADINFO info{sizeof(info)};
      if (GetGUIThreadInfo(GetCurrentThreadId(),&info) && info.hwndCaret) {
        caret_ = info.rcCaret;
        MapWindowPoints(info.hwndCaret,nullptr,reinterpret_cast<POINT*>(&caret_),2);
      } else { candidates_.Hide(); return; }
    }
    candidates_.Update(module,yanvy::ModuleDir(module),last_,schema_,ascii_,caret_,transient,[this](int index) {
      if (!context_ || !active_) return;
      ComPtr<ITfContextView> view; HWND window=nullptr;
      if (FAILED(context_->GetActiveView(&view)) || FAILED(view->GetWnd(&window)) ||
          GetAncestor(window,GA_ROOT) != GetForegroundWindow()) { Cancel(); return; }
      auto q = Request(yanvy::Op::Key); q.key = uint32_t('1'+index);
      yanvy::Response r;
      if (!yanvy::Call(q,r) || !Apply(context_.Get(),r)) Cancel();
    },[this]{Cancel();if(schema_){last_pinyin_schema_=schema_;schema_=0;}else schema_=last_pinyin_schema_;Show(true);});
  }
  LONG refs_ = 1;
  TfClientId client_ = TF_CLIENTID_NULL;
  DWORD thread_cookie_=TF_INVALID_COOKIE,focus_cookie_=TF_INVALID_COOKIE,edit_cookie_=TF_INVALID_COOKIE,layout_cookie_=TF_INVALID_COOKIE;
  ComPtr<ITfThreadMgr> manager_;
  ComPtr<ITfContext> context_;
  ComPtr<ITfComposition> composition_;
  ComPtr<yanvy::InputModeCompartment> mode_;
  GUID owner_{};
  uint64_t generation_ = 0;
  uint64_t snapshot_revision_ = 0;
  EditSessionOrder edit_order_;
  uint32_t schema_=1,preferred_schema_=1,last_pinyin_schema_=1;
  std::wstring preference_revision_;
  bool ascii_=false,active_=false,applying_=false,preserved_=false;
  RECT caret_{};
  yanvy::Response last_{};
  yanvy::CandidateWindow candidates_;
};

class Factory final : public IClassFactory {
 public:
  Factory(){ ++objects; }
  ~Factory(){ --objects; }
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** out) override {
    if (!out) return E_POINTER;
    *out = nullptr;
    if (iid != IID_IUnknown && iid != IID_IClassFactory) return E_NOINTERFACE;
    *out = static_cast<IClassFactory*>(this); AddRef(); return S_OK;
  }
  ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&refs_); }
  ULONG STDMETHODCALLTYPE Release() override { ULONG r=InterlockedDecrement(&refs_); if (!r) delete this; return r; }
  HRESULT STDMETHODCALLTYPE CreateInstance(IUnknown* outer, REFIID iid, void** out) override {
    if (!out) return E_POINTER;
    *out = nullptr;
    if (outer) return CLASS_E_NOAGGREGATION;
    auto service = new(std::nothrow) Service;
    if (!service) return E_OUTOFMEMORY;
    HRESULT hr = service->QueryInterface(iid,out); service->Release(); return hr;
  }
  HRESULT STDMETHODCALLTYPE LockServer(BOOL lock) override { if (lock) ++objects; else --objects; return S_OK; }
 private: LONG refs_ = 1;
};
}
STDAPI DllGetClassObject(REFCLSID clsid, REFIID iid, void** out) {
  if (!out) return E_POINTER;
  *out = nullptr;
  if (clsid != yanvy::kService) return CLASS_E_CLASSNOTAVAILABLE;
  auto factory = new(std::nothrow) Factory;
  if (!factory) return E_OUTOFMEMORY;
  HRESULT hr = factory->QueryInterface(iid,out); factory->Release(); return hr;
}
STDAPI DllCanUnloadNow() { return objects == 0 ? S_OK : S_FALSE; }
BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
  if (reason == DLL_PROCESS_ATTACH) { module = instance; DisableThreadLibraryCalls(instance); }
  return TRUE;
}
