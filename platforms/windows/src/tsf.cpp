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
#include "language_bar.hpp"
#include "msctf_compat.hpp"
#include <wrl/client.h>
#include <atomic>
#include <algorithm>
#include <new>
#ifdef YANVY_TSF_HOST_FIXTURE
#include "text_host_fixture.hpp"
#endif

using Microsoft::WRL::ComPtr;
namespace {
HINSTANCE module = nullptr;
std::atomic<long> objects{0};
#ifdef YANVY_TSF_HOST_FIXTURE
yanvy::TextHostDiagnostics host_diagnostics;
#endif

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
#ifdef YANVY_TSF_HOST_FIXTURE
    // Never let an unconfigured test DLL connect to a production namespace.
    if(yanvy::fixture_user_root.empty()||yanvy::test_object_suffix.empty())return E_UNEXPECTED;
#endif
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
      if (preserved_) {
        // Preserve the release as well: some hosts stop forwarding raw keyup
        // after the keyboard compartment closes. Unrelated releases pass on.
        TF_PRESERVEDKEY release{VK_SPACE,TF_MOD_ON_KEYUP|TF_MOD_IGNORE_ALL_MODIFIER};
        preserved_release_ = SUCCEEDED(keys->PreserveKey(client_,yanvy::kToggleEnglishRelease,&release,description,UINT(wcslen(description))));
      }
    }
    if (SUCCEEDED(hr)) hr = manager_->QueryInterface(IID_PPV_ARGS(&source));
    if (SUCCEEDED(hr)) hr = source->AdviseSink(IID_ITfThreadMgrEventSink, static_cast<ITfThreadMgrEventSink*>(this), &thread_cookie_);
    if (SUCCEEDED(hr)) source->AdviseSink(IID_ITfThreadFocusSink, static_cast<ITfThreadFocusSink*>(this), &focus_cookie_);
    if (FAILED(hr)) { Deactivate(); return hr; }
    AttachInputMode();
    AttachLanguageBar();
    ComPtr<ITfDocumentMgr> document;
    if (SUCCEEDED(manager_->GetFocus(&document))) Bind(document.Get());
#ifndef YANVY_MODE_FIXTURE
    yanvy::StartBroker(yanvy::ModuleDir(module));
#endif
#ifdef YANVY_TSF_HOST_FIXTURE
    InterlockedIncrement(&host_diagnostics.activations);
#endif
    return S_OK;
  }
  HRESULT STDMETHODCALLTYPE Deactivate() override {
    shortcut_held_ = false;
    DetachLanguageBar();
    if (!manager_) return S_OK;
#ifdef YANVY_TSF_HOST_FIXTURE
    InterlockedIncrement(&host_diagnostics.deactivations);
#endif
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
      if (preserved_release_) { TF_PRESERVEDKEY release{VK_SPACE,TF_MOD_ON_KEYUP|TF_MOD_IGNORE_ALL_MODIFIER};keys->UnpreserveKey(yanvy::kToggleEnglishRelease,&release); }
      keys->UnadviseKeyEventSink(client_);
    }
    preserved_=preserved_release_=false;
    manager_.Reset(); active_ = false;
    return S_OK;
  }
  HRESULT STDMETHODCALLTYPE OnSetFocus(BOOL foreground) override {
    // A key-sink focus notification is not a Space release. Actual thread
    // focus loss and deactivation have their own gesture-reset paths.
    if (!foreground) Cancel();
    return S_OK;
  }
  HRESULT STDMETHODCALLTYPE OnSetThreadFocus() override { RefreshPreference();return S_OK; }
  HRESULT STDMETHODCALLTYPE OnKillThreadFocus() override {
#ifdef YANVY_TSF_HOST_FIXTURE
    InterlockedIncrement(&host_diagnostics.thread_blur);
#endif
    shortcut_held_ = false; Cancel(); return S_OK;
  }
  HRESULT STDMETHODCALLTYPE OnInitDocumentMgr(ITfDocumentMgr*) override { return S_OK; }
  HRESULT STDMETHODCALLTYPE OnUninitDocumentMgr(ITfDocumentMgr*) override { return S_OK; }
  HRESULT STDMETHODCALLTYPE OnPushContext(ITfContext*) override { Cancel(); return S_OK; }
  HRESULT STDMETHODCALLTYPE OnPopContext(ITfContext*) override { Cancel(); return S_OK; }
  HRESULT STDMETHODCALLTYPE OnSetFocus(ITfDocumentMgr* focus, ITfDocumentMgr*) override {
#ifdef YANVY_TSF_HOST_FIXTURE
    InterlockedIncrement(&host_diagnostics.document_focus);
#endif
    ComPtr<ITfContext> next;
    if(focus)focus->GetTop(&next);
    // Repeated notifications for the same context must not erase preedit.
    // Real focus loss and a different top context still cancel before binding.
    if(next.Get()!=context_.Get()){Cancel();BindContext(next.Get());}
    RefreshPreference();return S_OK;
  }
  HRESULT STDMETHODCALLTYPE OnTestKeyDown(ITfContext* context, WPARAM key, LPARAM, BOOL* eaten) override {
#ifdef YANVY_TSF_HOST_FIXTURE
    InterlockedIncrement(&host_diagnostics.test_down);
#endif
    if (!eaten) return E_POINTER;
    *eaten = Interested(context,key); return S_OK;
  }
  HRESULT STDMETHODCALLTYPE OnKeyDown(ITfContext* context, WPARAM key, LPARAM lp, BOOL* eaten) override {
#ifdef YANVY_TSF_HOST_FIXTURE
    InterlockedIncrement(&host_diagnostics.key_down);
#endif
    if (!eaten) return E_POINTER;
    *eaten = FALSE;
    if (!Interested(context,key)) return S_OK;
    try {
      RefreshPreference();
      if (context_.Get() != context) { Cancel(); BindContext(context); }
      if (key == VK_SPACE && Down(VK_CONTROL)) {
        // Holding the shortcut must not toggle back on each autorepeat.
        // LPARAM bit 30 is the key's previous state, as for WM_KEYDOWN.
        if (!(lp & (LPARAM(1) << 30))) ToggleShortcut(true);
        *eaten = TRUE; return S_OK;
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
  HRESULT STDMETHODCALLTYPE OnTestKeyUp(ITfContext*, WPARAM key, LPARAM, BOOL* eaten) override {
    if (!eaten) return E_POINTER;
    // Request the release even when Ctrl was released before Space. Without
    // this, a preserved key's repeats cannot be distinguished from a new press.
    *eaten = key == VK_SPACE && shortcut_held_; return S_OK;
  }
  HRESULT STDMETHODCALLTYPE OnKeyUp(ITfContext*, WPARAM key, LPARAM, BOOL* eaten) override {
    if (!eaten) return E_POINTER;
    *eaten = key == VK_SPACE && shortcut_held_;
    if (key == VK_SPACE) shortcut_held_ = false;
    return S_OK;
  }
  HRESULT STDMETHODCALLTYPE OnPreservedKey(ITfContext* context, REFGUID key, BOOL* eaten) override {
#ifdef YANVY_TSF_HOST_FIXTURE
    InterlockedIncrement(&host_diagnostics.preserved);
#endif
    if (!eaten) return E_POINTER;
    *eaten = FALSE;
    if (key == yanvy::kToggleEnglishRelease) {
      *eaten = shortcut_held_;
      shortcut_held_ = false;
      return S_OK;
    }
    if (key == yanvy::kToggleEnglish && manager_ && !Disabled(context) &&
        !Down(VK_SHIFT) && !Down(VK_MENU) && !Down(VK_LWIN) && !Down(VK_RWIN)) {
      // A physical preserved key and the raw fallback share one press latch.
      // A programmatically invoked command with no Space held is independent.
      ToggleShortcut(Down(VK_SPACE)); *eaten=TRUE;
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
        if (!covered) {
#ifdef YANVY_TSF_HOST_FIXTURE
          InterlockedIncrement(&host_diagnostics.selection_cancel);
#endif
          Cancel();
        }
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
    manager_ = manager; client_ = client; preserved_release_=true; AttachInputMode();AttachLanguageBar();
  }
  yanvy::ModeLanguageBar* ModeButtonFixture() const {return language_bar_.Get();}
  void EndModeFixture() {
    DetachLanguageBar();
    if (mode_) { mode_->Detach(); mode_.Reset(); }
    manager_.Reset(); preserved_release_=false;
  }
  void SeedPendingFocusFixture(ITfContext* context) {
    RefreshPreference();BindContext(context);
    last_.input[0]=last_.preedit[0]=L'n';
  }
  bool HasPendingFocusFixture() const { return last_.input[0]!=0&&last_.preedit[0]!=0; }
#endif
 private:
  void AttachLanguageBar() {
    try {
      language_bar_.Attach(new yanvy::ModeLanguageBar(yanvy::kService,ascii_,[this]{
        ComPtr<ITfTextInputProcessorEx> keep_alive=this;
        ToggleInputMode();
      },[this]{
        ComPtr<ITfTextInputProcessorEx> keep_alive=this;
        return yanvy::OpenSettings(yanvy::ModuleDir(module));
      },&objects));
#if !defined(YANVY_MODE_FIXTURE) && !defined(YANVY_TSF_HOST_FIXTURE)
      ComPtr<ITfLangBarItemMgr> items;
      if(FAILED(manager_->QueryInterface(IID_ITfLangBarItemMgr,reinterpret_cast<void**>(items.GetAddressOf())))||
          FAILED(language_bar_binding_.Attach(items.Get(),language_bar_.Get())))DetachLanguageBar();
#endif
    }catch(...){DetachLanguageBar();}
    // UI integration is optional for TSF activation. Input keeps working if
    // a host cannot provide a language bar; no independent tray is recreated.
  }
  void DetachLanguageBar() {
    language_bar_binding_.Detach();
    if(language_bar_)language_bar_->Disconnect();
    language_bar_.Reset();
  }
  void AttachInputMode() {
    mode_.Attach(new(std::nothrow) yanvy::InputModeCompartment);
    if (mode_ && SUCCEEDED(mode_->Attach(manager_.Get(),client_,[this](bool open) {
      if (ascii_ != !open) { Cancel(); ascii_ = !open;if(language_bar_)language_bar_->SetAscii(ascii_); Show(true); }
    }))) {
      bool open = true; if (mode_->Read(open)) ascii_ = !open;
    } else mode_.Reset();
  }
  void ToggleInputMode() {
    const bool next_ascii = !ascii_;
    Cancel(); ascii_ = next_ascii;
    if(language_bar_)language_bar_->SetAscii(ascii_);
    if (mode_) mode_->SetOpen(!next_ascii);
    Show(true);
  }
  void ToggleShortcut(bool physical) {
    // If the host rejects release preservation, do not leave a latch that
    // may never receive its release. The raw path still rejects autorepeats.
    physical = physical && preserved_release_;
    if (physical && shortcut_held_) return;
    shortcut_held_ = physical;
    ToggleInputMode();
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
    if (key == VK_SPACE && Down(VK_CONTROL) && !Down(VK_SHIFT)) return true;
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
#ifdef YANVY_TSF_HOST_FIXTURE
    InterlockedIncrement(&host_diagnostics.broker_focus);
#endif
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
#ifdef YANVY_TSF_HOST_FIXTURE
    InterlockedIncrement(&host_diagnostics.cancellations);
#endif
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
  ComPtr<yanvy::ModeLanguageBar> language_bar_;
  yanvy::LanguageBarBinding language_bar_binding_;
  GUID owner_{};
  uint64_t generation_ = 0;
  uint64_t snapshot_revision_ = 0;
  EditSessionOrder edit_order_;
  uint32_t schema_=1,preferred_schema_=1,last_pinyin_schema_=1;
  std::wstring preference_revision_;
  bool ascii_=false,active_=false,applying_=false,preserved_=false,preserved_release_=false,shortcut_held_=false;
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
#ifdef YANVY_TSF_HOST_FIXTURE
extern "C" HRESULT WINAPI YanvyGetTextHostDiagnostics(yanvy::TextHostDiagnostics* value,DWORD bytes) {
  if(!value||bytes!=sizeof(*value))return E_INVALIDARG;
  value->activations=InterlockedCompareExchange(&host_diagnostics.activations,0,0);
  value->deactivations=InterlockedCompareExchange(&host_diagnostics.deactivations,0,0);
  value->test_down=InterlockedCompareExchange(&host_diagnostics.test_down,0,0);
  value->key_down=InterlockedCompareExchange(&host_diagnostics.key_down,0,0);
  value->preserved=InterlockedCompareExchange(&host_diagnostics.preserved,0,0);
  value->cancellations=InterlockedCompareExchange(&host_diagnostics.cancellations,0,0);
  value->thread_blur=InterlockedCompareExchange(&host_diagnostics.thread_blur,0,0);
  value->document_focus=InterlockedCompareExchange(&host_diagnostics.document_focus,0,0);
  value->selection_cancel=InterlockedCompareExchange(&host_diagnostics.selection_cancel,0,0);
  value->broker_focus=InterlockedCompareExchange(&host_diagnostics.broker_focus,0,0);
  return S_OK;
}
extern "C" HRESULT WINAPI YanvyConfigureTextHost(const wchar_t* suffix,const wchar_t* user_root) {
  if(objects.load()!=0||!suffix||!user_root)return E_UNEXPECTED;
  const std::wstring selected_suffix=suffix;
  const std::filesystem::path selected_root=user_root;
  if(selected_suffix.size()!=32||!std::all_of(selected_suffix.begin(),selected_suffix.end(),[](wchar_t c){return(c>=L'0'&&c<=L'9')||(c>=L'a'&&c<=L'f');})||
      selected_root.empty()||selected_root.lexically_normal()!=selected_root||!yanvy::PlainLocalPath(selected_root))return E_INVALIDARG;
  GUID toggle{},release{};
  if(FAILED(CoCreateGuid(&toggle))||FAILED(CoCreateGuid(&release)))return E_FAIL;
  yanvy::fixture_user_root=selected_root;yanvy::test_object_suffix=selected_suffix;
  yanvy::kToggleEnglish=toggle;yanvy::kToggleEnglishRelease=release;
  return S_OK;
}
#endif
BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
  if (reason == DLL_PROCESS_ATTACH) { module = instance; DisableThreadLibraryCalls(instance); }
  return TRUE;
}
