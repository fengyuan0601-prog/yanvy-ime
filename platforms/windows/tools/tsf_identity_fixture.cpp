// SPDX-License-Identifier: GPL-3.0-or-later
// Real TSF registration is process-local, hidden and uses random identities.
// No production profile, COM registry, GUI, broker or desktop input is touched.
// This proves COM identity coexistence and preserved-command routing, not
// Windows input-switcher migration or hardware keyboard delivery in an app.
#define YANVY_MODE_FIXTURE
#include "../src/tsf.cpp"
#include <iostream>
#include <stdexcept>

namespace {
// Constants from Microsoft's WinSDK msctf.h, absent in older MinGW headers.
constexpr DWORD local_process = 0x00000004;
constexpr DWORD hidden_profile = 0x00000002;
void Check(bool ok, const char* name) {
  if (!ok) throw std::runtime_error(name);
  std::cout << "PASS " << name << '\n';
}
void Require(HRESULT hr, const char* name) { Check(SUCCEEDED(hr),name); }
struct Counts { int created=0,activated=0,deactivated=0; };
class CounterTip final : public ITfTextInputProcessor {
 public:
  explicit CounterTip(Counts& counts):counts_(counts) { ++counts_.created; }
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid,void** out) override {
    if (!out) return E_POINTER;
    *out=nullptr;
    if (iid!=IID_IUnknown && iid!=IID_ITfTextInputProcessor) return E_NOINTERFACE;
    *out=static_cast<ITfTextInputProcessor*>(this); AddRef(); return S_OK;
  }
  ULONG STDMETHODCALLTYPE AddRef() override { return ++refs_; }
  ULONG STDMETHODCALLTYPE Release() override { auto n=--refs_; if (!n) delete this; return n; }
  HRESULT STDMETHODCALLTYPE Activate(ITfThreadMgr*,TfClientId) override { ++counts_.activated; return S_OK; }
  HRESULT STDMETHODCALLTYPE Deactivate() override { ++counts_.deactivated; return S_OK; }
 private: ULONG refs_=1; Counts& counts_;
};
class CounterFactory final : public IClassFactory {
 public:
  explicit CounterFactory(Counts& counts):counts_(counts) {}
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid,void** out) override {
    if (!out) return E_POINTER;
    *out=nullptr;
    if (iid!=IID_IUnknown && iid!=IID_IClassFactory) return E_NOINTERFACE;
    *out=static_cast<IClassFactory*>(this); AddRef(); return S_OK;
  }
  ULONG STDMETHODCALLTYPE AddRef() override { return ++refs_; }
  ULONG STDMETHODCALLTYPE Release() override { auto n=--refs_; if (!n) delete this; return n; }
  HRESULT STDMETHODCALLTYPE CreateInstance(IUnknown* outer,REFIID iid,void** out) override {
    if (outer) return CLASS_E_NOAGGREGATION;
    held.Attach(new CounterTip(counts_)); // Model a host retaining the old TIP.
    return held->QueryInterface(iid,out);
  }
  HRESULT STDMETHODCALLTYPE LockServer(BOOL) override { return S_OK; }
  ComPtr<ITfTextInputProcessor> held;
 private: ULONG refs_=1; Counts& counts_;
};
class LocalProfile {
 public:
  LocalProfile(ITfInputProcessorProfileMgr* profiles,LANGID language,IClassFactory* factory)
      : profiles_(profiles),language_(language) {
    if (FAILED(CoCreateGuid(&clsid_)) || FAILED(CoCreateGuid(&profile_))) throw std::runtime_error("random identity");
    const HRESULT hr=CoRegisterClassObject(clsid_,factory,CLSCTX_INPROC_SERVER,REGCLS_MULTIPLEUSE,&cookie_);
    if (FAILED(hr)) throw std::runtime_error("process-local COM factory");
    const wchar_t name[]=L"YANVY isolated fixture";
    const HRESULT registration=profiles_->RegisterProfile(clsid_,language_,profile_,name,UINT(wcslen(name)),
        L"",0,0,nullptr,0,TRUE,local_process|hidden_profile);
    if (FAILED(registration)) {
      profiles_->UnregisterProfile(clsid_,language_,profile_,local_process);
      CoRevokeClassObject(cookie_); cookie_=0;
      throw std::runtime_error("process-local TSF registration");
    }
  }
  ~LocalProfile() {
    profiles_->UnregisterProfile(clsid_,language_,profile_,local_process);
    if (cookie_) CoRevokeClassObject(cookie_);
  }
 private:
  ComPtr<ITfInputProcessorProfileMgr> profiles_;
  LANGID language_; GUID clsid_{},profile_{}; DWORD cookie_=0;
};
struct KeyboardState {
  BYTE saved[256]{},keys[256]{};
  KeyboardState() { if (!GetKeyboardState(saved)) throw std::runtime_error("fixture keyboard snapshot"); }
  ~KeyboardState() { SetKeyboardState(saved); }
  void Press(bool control,bool space) {
    ZeroMemory(keys,sizeof(keys));
    if (control) keys[VK_CONTROL]=keys[VK_LCONTROL]=0x80;
    if (space) keys[VK_SPACE]=0x80;
    if (!SetKeyboardState(keys)) throw std::runtime_error("fixture thread keyboard state");
  }
};
}
int wmain() {
  if (FAILED(CoCreateGuid(&yanvy::kToggleEnglish)) || FAILED(CoCreateGuid(&yanvy::kToggleEnglishRelease))) return 1;
  if (FAILED(CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED))) return 1;
  int result=1;
  try {
    // TSF can retain an instance until manager shutdown, beyond local profile
    // unregistration. Keep its counters alive for that final Deactivate call.
    Counts old_counts,new_counts;
    ComPtr<ITfThreadMgr> manager;
    ComPtr<ITfInputProcessorProfileMgr> profiles;
    Require(CoCreateInstance(CLSID_TF_ThreadMgr,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&manager)),"private TSF manager");
    Require(CoCreateInstance(CLSID_TF_InputProcessorProfiles,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&profiles)),"profile manager");
    TfClientId client=TF_CLIENTID_NULL;
    Require(manager->Activate(&client),"private manager activation");
    struct Deactivate { ITfThreadMgr* p; ~Deactivate(){p->Deactivate();} } deactivate{manager.Get()};
    const LANGID language=LOWORD(GetKeyboardLayout(0));
    const DWORD pid=GetCurrentProcessId();
    {
      ComPtr<CounterFactory> old_factory,new_factory;
      old_factory.Attach(new CounterFactory(old_counts)); new_factory.Attach(new CounterFactory(new_counts));
      LocalProfile old_profile(profiles.Get(),language,old_factory.Get());
      Check(old_counts.created==1 && old_counts.activated==1,"old identity instantiated by TSF");
      auto held_old=old_factory->held;
      LocalProfile new_profile(profiles.Get(),language,new_factory.Get());
      Check(new_counts.created==1 && new_counts.activated==1,"new identity instantiated in same running process");
      Check(held_old && old_factory->held.Get()==held_old.Get() &&
          new_factory->held.Get()!=held_old.Get() && pid==GetCurrentProcessId(),"retained old instance does not replace new class instance");
    }
    {
      ComPtr<Factory> factory; factory.Attach(new Factory);
      LocalProfile current(profiles.Get(),language,factory.Get());
      ComPtr<ITfKeystrokeMgr> keys; Require(manager.As(&keys),"real keystroke manager");
      TF_PRESERVEDKEY release{VK_SPACE,TF_MOD_ON_KEYUP|TF_MOD_IGNORE_ALL_MODIFIER};
      BOOL registered=FALSE;
      Require(keys->IsPreservedKey(yanvy::kToggleEnglishRelease,&release,&registered),"release registration lookup");
      Check(registered,"release shortcut accepted by real TSF");
      ComPtr<ITfDocumentMgr> doc; ComPtr<ITfContext> context; TfEditCookie cookie;
      Require(manager->CreateDocumentMgr(&doc),"private document manager");
      Require(doc->CreateContext(client,0,nullptr,&context,&cookie),"private empty context");
      Require(doc->Push(context.Get()),"context attached to private document");
      struct Pop { ITfDocumentMgr* p; ~Pop(){p->Pop(TF_POPF_ALL);} } pop{doc.Get()};
      ComPtr<ITfCompartmentMgr> compartments; ComPtr<ITfCompartment> mode;
      Require(manager.As(&compartments),"private compartments");
      Require(compartments->GetCompartment(GUID_COMPARTMENT_KEYBOARD_OPENCLOSE,&mode),"private input mode");
      VARIANT value; VariantInit(&value); value.vt=VT_I4; value.lVal=1;
      Require(mode->SetValue(client,&value),"start fixture in Chinese mode");
      auto is_open=[&](bool expected) { VARIANT v;VariantInit(&v);bool ok=SUCCEEDED(mode->GetValue(&v))&&v.vt==VT_I4&&(v.lVal!=0)==expected;VariantClear(&v);return ok; };
      auto command=[&](REFGUID guid,bool expected_eaten) { BOOL eaten=FALSE;return SUCCEEDED(keys->SimulatePreservedKey(context.Get(),guid,&eaten)) && bool(eaten)==expected_eaten; };
      KeyboardState keyboard;
      for (bool next : {false,true,false,true}) {
        keyboard.Press(true,true);
        Check(command(yanvy::kToggleEnglish,true) && is_open(next),"preserved command switches through real TSF route");
        for (int repeat=0;repeat<16;++repeat)
          if (!command(yanvy::kToggleEnglish,true) || !is_open(next)) throw std::runtime_error("preserved command repeated toggle");
        keyboard.Press(false,false);
        Check(command(yanvy::kToggleEnglishRelease,true) && is_open(next),"preserved release clears gesture without changing mode");
        Check(command(yanvy::kToggleEnglishRelease,false),"ordinary Space release is not consumed");
      }
    }
    result=0;
  } catch (const std::exception& e) { std::cerr << "FAIL " << e.what() << '\n'; }
  CoUninitialize();
  if (!result) std::cout << "Local TSF identity coexistence and preserved command routing PASS (not an application/Win+Space test)\n";
  return result;
}
