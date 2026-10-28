// SPDX-License-Identifier: GPL-3.0-or-later
// Registration sequence adapted from WeaselTSF/Register.cpp; see provenance.
#include "common.hpp"
#include "ids.hpp"
#include <msctf.h>
#include "msctf_compat.hpp"
#include "registration_flow.hpp"
#include "machine_store.hpp"
#include "broker_upgrade.hpp"
#include <wrl/client.h>
#include <iostream>
#include <algorithm>
#include <cstring>

using Microsoft::WRL::ComPtr;
namespace {
const GUID* const categories[] = { &GUID_TFCAT_TIP_KEYBOARD };
template<class Function> Function LoadProcedure(HMODULE module,const char* name) {
  const auto address=GetProcAddress(module,name);Function result=nullptr;
  static_assert(sizeof(result)==sizeof(address));
  std::memcpy(&result,&address,sizeof(result));return result;
}
HRESULT VerifyServiceIdentity(const std::filesystem::path& dll) {
  HMODULE library=LoadLibraryExW(dll.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
  if(!library)return HRESULT_FROM_WIN32(GetLastError());
  using GetFactory=HRESULT(WINAPI*)(REFCLSID,REFIID,void**);
  const auto get_factory=LoadProcedure<GetFactory>(library,"DllGetClassObject");
  ComPtr<IClassFactory> factory;
  const HRESULT hr=get_factory?get_factory(yanvy::kService,IID_PPV_ARGS(&factory)):E_NOINTERFACE;
  factory.Reset();FreeLibrary(library);return hr;
}
HRESULT SetString(HKEY root, const std::wstring& path, const wchar_t* name, const std::wstring& value, REGSAM view=KEY_WOW64_64KEY) {
  HKEY key = nullptr;
  LSTATUS error = RegCreateKeyExW(root,path.c_str(),0,nullptr,0,KEY_WRITE|view,nullptr,&key,nullptr);
  if (error != ERROR_SUCCESS) return HRESULT_FROM_WIN32(error);
  error = RegSetValueExW(key,name,0,REG_SZ,reinterpret_cast<const BYTE*>(value.c_str()),DWORD((value.size()+1)*sizeof(wchar_t)));
  RegCloseKey(key);
  return HRESULT_FROM_WIN32(error);
}
HRESULT DeleteComClass(HKEY hive, REGSAM view, const wchar_t* clsid=yanvy::kClsid) {
  HKEY parent=nullptr;
  LSTATUS error=RegOpenKeyExW(hive,L"Software\\Classes\\CLSID",0,KEY_READ|KEY_WRITE|view,&parent);
  if (error==ERROR_FILE_NOT_FOUND||error==ERROR_PATH_NOT_FOUND) return S_OK;
  if (error!=ERROR_SUCCESS) return HRESULT_FROM_WIN32(error);
  error=RegDeleteTreeW(parent,clsid);RegCloseKey(parent);
  if (error==ERROR_FILE_NOT_FOUND||error==ERROR_PATH_NOT_FOUND) return S_OK;
  return HRESULT_FROM_WIN32(error);
}
[[maybe_unused]] HRESULT Register(bool machine, const std::filesystem::path& runtime = yanvy::ModuleDir()) {
  const auto dll = runtime / L"YANVY-TSF.dll";
  const auto dll32 = runtime / L"YANVY-TSF-x86.dll";
  std::error_code file_error;
  if (!std::filesystem::is_regular_file(dll,file_error)||!std::filesystem::is_regular_file(dll32,file_error)) return HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
  // A protected legacy directory can still contain an older, incompatible
  // service CLSID. Verify the target before touching any working registration.
  HRESULT identity=VerifyServiceIdentity(dll);if(FAILED(identity))return identity;
  const std::wstring key = std::wstring(L"Software\\Classes\\CLSID\\") + yanvy::kClsid;
  const HKEY hive = machine ? HKEY_LOCAL_MACHINE : HKEY_CURRENT_USER;
  HRESULT hr = SetString(hive,key,nullptr,yanvy::kName);
  if (SUCCEEDED(hr)) hr = SetString(hive,key+L"\\InprocServer32",nullptr,dll.wstring());
  if (SUCCEEDED(hr)) hr = SetString(hive,key+L"\\InprocServer32",L"ThreadingModel",L"Apartment");
  if (SUCCEEDED(hr)) hr = SetString(hive,key,nullptr,yanvy::kName,KEY_WOW64_32KEY);
  if (SUCCEEDED(hr)) hr = SetString(hive,key+L"\\InprocServer32",nullptr,dll32.wstring(),KEY_WOW64_32KEY);
  if (SUCCEEDED(hr)) hr = SetString(hive,key+L"\\InprocServer32",L"ThreadingModel",L"Apartment",KEY_WOW64_32KEY);
  if (FAILED(hr)) { DeleteComClass(hive,KEY_WOW64_32KEY);DeleteComClass(hive,KEY_WOW64_64KEY);return hr; }
  ComPtr<ITfInputProcessorProfileMgr> profiles;
  hr = CoCreateInstance(CLSID_TF_InputProcessorProfiles,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&profiles));
  if (SUCCEEDED(hr)) hr = profiles->RegisterProfile(yanvy::kService,yanvy::kLanguage,yanvy::kProfile,
      yanvy::kName,ULONG(wcslen(yanvy::kName)),dll.c_str(),ULONG(dll.wstring().size()),0,nullptr,0,TRUE,0);
  std::cout << "register_profile=0x" << std::hex << static_cast<unsigned long>(hr) << '\n';
  ComPtr<ITfCategoryMgr> category;
  if (SUCCEEDED(hr)) hr = CoCreateInstance(CLSID_TF_CategoryMgr,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&category));
  if (SUCCEEDED(hr)) for (auto guid : categories) {
    hr = category->RegisterCategory(yanvy::kService,*guid,yanvy::kService);
    std::cout << "register_category=0x" << std::hex << static_cast<unsigned long>(hr) << '\n';
    if (FAILED(hr)) break;
  }
  if (FAILED(hr)) {
    HRESULT cleanup = profiles ? profiles->UnregisterProfile(yanvy::kService,yanvy::kLanguage,yanvy::kProfile,0) : S_OK;
    if (SUCCEEDED(cleanup) && category) for (auto guid : categories) {
      cleanup = category->UnregisterCategory(yanvy::kService,*guid,yanvy::kService);
      if (FAILED(cleanup)) break;
    }
    if (SUCCEEDED(cleanup)) { DeleteComClass(hive,KEY_WOW64_32KEY);DeleteComClass(hive,KEY_WOW64_64KEY); }
  }
  return hr;
}
[[maybe_unused]] HRESULT EnableUserProfile() {
  // Called by the original unelevated installer UI, not its UAC worker.
  HRESULT hr=yanvy::ValidateMachineRuntime(yanvy::ModuleDir());
  ComPtr<ITfInputProcessorProfiles> profiles;
  if(SUCCEEDED(hr))hr=CoCreateInstance(CLSID_TF_InputProcessorProfiles,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&profiles));
  if(SUCCEEDED(hr))hr=profiles->EnableLanguageProfile(yanvy::kService,yanvy::kLanguage,yanvy::kProfile,TRUE);
  if(SUCCEEDED(hr))hr=SetString(HKEY_CURRENT_USER,L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",L"YANVY-dev",
      L"\""+(yanvy::ModuleDir()/L"YANVY-Server.exe").wstring()+L"\" --serve");
  return hr;
}
HRESULT FindGuid(IEnumGUID* list, REFGUID expected, bool& found) {
  found = false;
  GUID value{}; ULONG fetched = 0;
  for (;;) {
    HRESULT hr = list->Next(1,&value,&fetched);
    if (FAILED(hr)) return hr;
    if (hr == S_FALSE && fetched == 0) return S_OK;
    if (fetched != 1) return E_UNEXPECTED;
    if (value == expected) { found = true; return S_OK; }
  }
}
HRESULT RemovalResult(LSTATUS error) {
  if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND) return S_OK;
  return HRESULT_FROM_WIN32(error);
}
HRESULT ProfilePresent(REFCLSID service, REFGUID profile, bool& present) {
  present=false;ComPtr<ITfInputProcessorProfiles> profiles;
  HRESULT hr=CoCreateInstance(CLSID_TF_InputProcessorProfiles,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&profiles));
  ComPtr<IEnumTfLanguageProfiles> list;
  if(SUCCEEDED(hr))hr=profiles->EnumLanguageProfiles(yanvy::kLanguage,&list);
  if(FAILED(hr))return hr;
  if(!list)return E_UNEXPECTED;
  TF_LANGUAGEPROFILE value{};ULONG count=0;
  for(;;){
    hr=list->Next(1,&value,&count);if(FAILED(hr))return hr;
    if(count==0)return S_OK;
    if(value.clsid==service&&value.guidProfile==profile){present=true;return S_OK;}
  }
}
struct RegistrationRemoval {
  bool machine;
  const GUID* service=&yanvy::kService;
  const GUID* profile=&yanvy::kProfile;
  const wchar_t* clsid=yanvy::kClsid;
  HRESULT RemoveProfile() {
    bool present=false;HRESULT hr=ProfilePresent(*service,*profile,present);
    if (FAILED(hr) || !present) return hr;
    // Do not disable first: if removal is refused the existing profile must
    // remain usable. TSF's Unregister operation removes the service itself.
    ComPtr<ITfInputProcessorProfileMgr> profiles;
    hr=CoCreateInstance(CLSID_TF_InputProcessorProfiles,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&profiles));
    return SUCCEEDED(hr)?profiles->UnregisterProfile(*service,yanvy::kLanguage,*profile,0):hr;
  }
  HRESULT RemoveCategories() {
    ComPtr<ITfCategoryMgr> category;
    HRESULT hr = CoCreateInstance(CLSID_TF_CategoryMgr,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&category));
    if (FAILED(hr)) return hr;
    for (auto guid : categories) {
      ComPtr<IEnumGUID> list;
      hr = category->EnumCategoriesInItem(*service,&list);
      if (FAILED(hr)) return hr;
      if (!list) return E_UNEXPECTED;
      bool present = false;
      hr = FindGuid(list.Get(),*guid,present);
      if (FAILED(hr)) return hr;
      if (present) {
        hr = category->UnregisterCategory(*service,*guid,*service);
        if (FAILED(hr)) return hr;
      }
    }
    return S_OK;
  }
  HRESULT RemoveComServer() {
    HKEY hive=machine ? HKEY_LOCAL_MACHINE : HKEY_CURRENT_USER;
    HRESULT hr=DeleteComClass(hive,KEY_WOW64_32KEY,clsid);
    return SUCCEEDED(hr) ? DeleteComClass(hive,KEY_WOW64_64KEY,clsid) : hr;
  }
  HRESULT RemoveStartup() {
    // A machine uninstall can be authorized with another administrator's
    // account. The original UI removes its own Run value separately.
    if(machine)return S_OK;
    HKEY run = nullptr;
    LSTATUS error = RegOpenKeyExW(HKEY_CURRENT_USER,L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",0,KEY_SET_VALUE,&run);
    if (error != ERROR_SUCCESS) return RemovalResult(error);
    error = RegDeleteValueW(run,L"YANVY-dev");
    RegCloseKey(run);
    return RemovalResult(error);
  }
};
HRESULT RetireLegacy(bool machine) {
  RegistrationRemoval removal{machine,&yanvy::kLegacyService,&yanvy::kLegacyProfile,yanvy::kLegacyClsid};
  return yanvy::UnregisterTextService(removal);
}
[[maybe_unused]] HRESULT Unregister(bool machine, bool include_legacy=true) {
  if(include_legacy){HRESULT hr=RetireLegacy(machine);if(FAILED(hr))return hr;}
  RegistrationRemoval removal{machine};
  return yanvy::UnregisterTextService(removal);
}
[[maybe_unused]] HRESULT StopPreviousBrokers(const std::wstring& sid, DWORD session) {
  if (sid.empty()) return E_INVALIDARG;
  HRESULT result = S_OK;
  // rc.2/rc.3 use v2 and are covered by the current broker's legacy bridge.
  // Leaving their event out lets their engine survive upgrades indefinitely.
  for (const auto version : {L"v2", L"v3", L"v4", L"v5", L"v6", L"v7"}) {
    const auto name = std::wstring(L"Local\\Stop-YANVY-") + version + L"-" + sid + L"-" + std::to_wstring(session);
    HANDLE event = OpenEventW(EVENT_MODIFY_STATE, FALSE, name.c_str());
    if (event) { if (!SetEvent(event)) result = HRESULT_FROM_WIN32(GetLastError()); CloseHandle(event); }
    else if (GetLastError() != ERROR_FILE_NOT_FOUND) result = HRESULT_FROM_WIN32(GetLastError());
  }
  return result;
}
}
#ifndef YANVY_SETUP_REGISTRY_FIXTURE
int wmain(int argc, wchar_t** argv) {
  HRESULT init = CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
  if (FAILED(init)) return 1;
  const std::wstring option = argc > 1 ? argv[1] : L"--status";
  const bool machine = argc > 2 && std::wstring(argv[2]) == L"--machine";
  HRESULT hr = E_INVALIDARG;
  if (option == L"--register" && machine && argc==3) {
    hr = yanvy::ValidateMachineRuntime(yanvy::ModuleDir());
    if(SUCCEEDED(hr))hr = Register(true);
  }
  else if (option == L"--restore-registration" && machine && argc==4) {
    // Recovery runs this trusted helper, never a previous installation's exe.
    // An old user-writable COM target is deliberately not reintroduced.
    hr = yanvy::ValidateMachineRuntime(yanvy::ModuleDir());
    const auto previous=std::filesystem::path(argv[3]);
    if(SUCCEEDED(hr))hr = yanvy::ValidateMachineRuntime(previous);
    if(SUCCEEDED(hr))hr = Register(true,previous);
  }
  else if ((option == L"--unregister"||option==L"--unregister-current"||option==L"--retire-legacy") && machine && argc==3) {
    hr = yanvy::ValidateMachineRuntime(yanvy::ModuleDir());
    if(SUCCEEDED(hr))hr = option==L"--retire-legacy"?RetireLegacy(true):Unregister(true,option==L"--unregister");
  }
  else if (option == L"--enable-user" && argc==2) hr=EnableUserProfile();
  else if (option == L"--verify-current-broker" && argc==2) {
    hr=yanvy::ValidateMachineRuntime(yanvy::ModuleDir());
    if(SUCCEEDED(hr))hr=yanvy::VerifyCurrentUpgradeBroker(yanvy::ModuleDir(),yanvy::CurrentUpgradeBrokerObjects());
  }
  else if (option == L"--disable-user" && argc==2) {
    hr=yanvy::ValidateMachineRuntime(yanvy::ModuleDir());
    if(SUCCEEDED(hr)){RegistrationRemoval removal{false};hr=removal.RemoveStartup();}
  }
  else if (option == L"--validate-machine-runtime" && argc==3) hr=yanvy::ValidateMachineRuntime(std::filesystem::path(argv[2]));
  else if (option == L"--validate-previous-cleanup" && argc==3) hr=yanvy::ValidatePreviousCleanup(std::filesystem::path(argv[2]));
  else if (option == L"--has-legacy-profile" && argc==2) {
    bool present=false;hr=ProfilePresent(yanvy::kLegacyService,yanvy::kLegacyProfile,present);
    if(SUCCEEDED(hr)&&!present)hr=HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
  }
  else if (option == L"--stop-previous-brokers" && (argc==2||argc==5)) {
    // Close the superseded per-user brokers after a successful upgrade so
    // their notification icons cannot coexist with the new one. Do not run
    // executables from the user-selected previous directory as administrator.
    const auto sid = yanvy::UserSid();
    DWORD session = 0;
    if (sid.empty() || !ProcessIdToSessionId(GetCurrentProcessId(), &session)) hr = E_FAIL;
    else {
      hr=argc==5?yanvy::ValidateMachineRuntime(yanvy::ModuleDir()):S_OK;
      yanvy::UpgradeBrokerSnapshot snapshot;
      if(SUCCEEDED(hr)&&argc==5){
        try{
          size_t pid_end=0,created_end=0;const auto pid=std::stoull(argv[2],&pid_end),created=std::stoull(argv[3],&created_end);
          if(pid==0||pid>MAXDWORD||created==0||pid_end!=wcslen(argv[2])||created_end!=wcslen(argv[3]))hr=E_INVALIDARG;
          else snapshot={DWORD(pid),created};
        }catch(...){hr=E_INVALIDARG;}
      }
      if(SUCCEEDED(hr))hr=StopPreviousBrokers(sid,session);
      if(SUCCEEDED(hr)&&argc==5)hr=yanvy::StopCapturedUpgradeBroker(snapshot,argv[4],yanvy::ModuleDir(),yanvy::CurrentUpgradeBrokerObjects());
    }
  }
  else if (option == L"--check-dependencies") {
    // Load the packaged engine explicitly before starting the server. Missing
    // dependencies are reported without launching a child stuck in a loader UI.
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOOPENFILEERRORBOX);
    const auto dll=yanvy::ModuleDir()/L"rime.dll";
    HMODULE library=LoadLibraryExW(dll.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    hr=library&&GetProcAddress(library,"rime_get_api")?S_OK:E_FAIL;
    if(library)FreeLibrary(library);
  }
  else if (option == L"--status") {
    ComPtr<ITfInputProcessorProfiles> profiles;
    hr = CoCreateInstance(CLSID_TF_InputProcessorProfiles,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&profiles));
    BOOL enabled = FALSE;
    bool registered = false;
    ComPtr<IEnumTfLanguageProfiles> list;
    if (SUCCEEDED(hr)) hr = profiles->EnumLanguageProfiles(yanvy::kLanguage,&list);
    TF_LANGUAGEPROFILE profile{}; ULONG fetched = 0;
    if (SUCCEEDED(hr)) while (list->Next(1,&profile,&fetched) == S_OK && fetched)
      if (profile.clsid == yanvy::kService && profile.guidProfile == yanvy::kProfile) registered = true;
    if (registered) hr = profiles->IsEnabledLanguageProfile(yanvy::kService,yanvy::kLanguage,yanvy::kProfile,&enabled);
    std::cout << "profile_registered=" << (registered ? "true" : "false") << '\n';
    std::cout << "profile_enabled=" << (enabled ? "true" : "false") << '\n';
  } else if (option == L"--activate") {
    // Explicit testing/enable action only, never called by the installer.
    ComPtr<ITfInputProcessorProfileMgr> profiles;
    hr = CoCreateInstance(CLSID_TF_InputProcessorProfiles,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&profiles));
    if (SUCCEEDED(hr)) hr = profiles->ActivateProfile(TF_PROFILETYPE_INPUTPROCESSOR,yanvy::kLanguage,
        yanvy::kService,yanvy::kProfile,nullptr,TF_IPPMF_FORSESSION);
  } else if (option == L"--com-smoke") {
    // COM ABI smoke test only. TSF assigns a service client ID when it loads a
    // registered language profile; an application client ID cannot replace it.
    const auto dll = yanvy::ModuleDir()/L"YANVY-TSF.dll";
    HMODULE library = LoadLibraryExW(dll.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    hr = library ? S_OK : HRESULT_FROM_WIN32(GetLastError());
    if (library) {
      using GetFactory = HRESULT (WINAPI*)(REFCLSID,REFIID,void**);
      auto factory_function = LoadProcedure<GetFactory>(library,"DllGetClassObject");
      ComPtr<IClassFactory> factory;
      ComPtr<ITfTextInputProcessorEx> service;
      hr = factory_function ? factory_function(yanvy::kService,IID_PPV_ARGS(&factory)) : E_NOINTERFACE;
      if (SUCCEEDED(hr)) hr = factory->CreateInstance(nullptr,IID_ITfTextInputProcessorEx,reinterpret_cast<void**>(service.GetAddressOf()));
      ComPtr<ITfKeyEventSink> keys;
      if (SUCCEEDED(hr)) hr = service->QueryInterface(IID_PPV_ARGS(&keys));
      ComPtr<ITfCompositionSink> composition;
      if (SUCCEEDED(hr)) hr = service->QueryInterface(IID_PPV_ARGS(&composition));
      std::cout << "native_com_interfaces=0x" << std::hex << static_cast<unsigned long>(hr) << '\n';
      if (service) service->Deactivate();
      composition.Reset(); keys.Reset(); service.Reset(); factory.Reset();
      using CanUnload = HRESULT (WINAPI*)();
      auto can_unload = LoadProcedure<CanUnload>(library,"DllCanUnloadNow");
      if (SUCCEEDED(hr) && (!can_unload || can_unload() != S_OK)) hr = E_UNEXPECTED;
      if (SUCCEEDED(hr)) FreeLibrary(library);
    }
  }
  std::cout << "result=0x" << std::hex << static_cast<unsigned long>(hr) << '\n';
  CoUninitialize();
  return SUCCEEDED(hr) ? 0 : 1;
}
#endif
