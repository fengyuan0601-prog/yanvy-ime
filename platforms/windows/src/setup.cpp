// SPDX-License-Identifier: GPL-3.0-or-later
// Registration sequence adapted from WeaselTSF/Register.cpp; see provenance.
#include "common.hpp"
#include "ids.hpp"
#include <msctf.h>
#include "msctf_compat.hpp"
#include "registration_flow.hpp"
#include <wrl/client.h>
#include <iostream>
#include <algorithm>

using Microsoft::WRL::ComPtr;
namespace {
const GUID* const categories[] = { &GUID_TFCAT_TIP_KEYBOARD };
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
HRESULT Register(bool machine) {
  const auto dll = yanvy::ModuleDir() / L"YANVY-TSF.dll";
  const auto dll32 = yanvy::ModuleDir() / L"YANVY-TSF-x86.dll";
  std::error_code file_error;
  if (!std::filesystem::is_regular_file(dll32,file_error)) return HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
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
  // Enabling the new profile does not choose it as the user's default.
  if (SUCCEEDED(hr)) {
    ComPtr<ITfInputProcessorProfiles> legacy;
    hr = CoCreateInstance(CLSID_TF_InputProcessorProfiles,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&legacy));
    if (SUCCEEDED(hr)) hr = legacy->EnableLanguageProfile(yanvy::kService,yanvy::kLanguage,yanvy::kProfile,TRUE);
    std::cout << "enable_profile=0x" << std::hex << static_cast<unsigned long>(hr) << '\n';
  }
  if (SUCCEEDED(hr)) hr = SetString(HKEY_CURRENT_USER,L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",L"YANVY-dev",
      L"\"" + (yanvy::ModuleDir()/L"YANVY-Server.exe").wstring() + L"\" --serve");
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
struct RegistrationRemoval {
  bool machine;
  HRESULT RemoveProfile() {
    ComPtr<ITfInputProcessorProfiles> profiles;
    HRESULT hr = CoCreateInstance(CLSID_TF_InputProcessorProfiles,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&profiles));
    if (FAILED(hr)) return hr;
    ComPtr<IEnumGUID> list;
    hr = profiles->EnumInputProcessorInfo(&list);
    if (FAILED(hr)) return hr;
    if (!list) return E_UNEXPECTED;
    bool present = false;
    hr = FindGuid(list.Get(),yanvy::kService,present);
    if (FAILED(hr) || !present) return hr;
    // Do not disable first: if removal is refused the existing profile must
    // remain usable. TSF's Unregister operation removes the service itself.
    return profiles->Unregister(yanvy::kService);
  }
  HRESULT RemoveCategories() {
    ComPtr<ITfCategoryMgr> category;
    HRESULT hr = CoCreateInstance(CLSID_TF_CategoryMgr,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&category));
    if (FAILED(hr)) return hr;
    for (auto guid : categories) {
      ComPtr<IEnumGUID> list;
      hr = category->EnumCategoriesInItem(yanvy::kService,&list);
      if (FAILED(hr)) return hr;
      if (!list) return E_UNEXPECTED;
      bool present = false;
      hr = FindGuid(list.Get(),*guid,present);
      if (FAILED(hr)) return hr;
      if (present) {
        hr = category->UnregisterCategory(yanvy::kService,*guid,yanvy::kService);
        if (FAILED(hr)) return hr;
      }
    }
    return S_OK;
  }
  HRESULT RemoveComServer() {
    HKEY hive=machine ? HKEY_LOCAL_MACHINE : HKEY_CURRENT_USER;
    HRESULT hr=DeleteComClass(hive,KEY_WOW64_32KEY);
    return SUCCEEDED(hr) ? DeleteComClass(hive,KEY_WOW64_64KEY) : hr;
  }
  HRESULT RemoveStartup() {
    HKEY run = nullptr;
    LSTATUS error = RegOpenKeyExW(HKEY_CURRENT_USER,L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",0,KEY_SET_VALUE,&run);
    if (error != ERROR_SUCCESS) return RemovalResult(error);
    error = RegDeleteValueW(run,L"YANVY-dev");
    RegCloseKey(run);
    return RemovalResult(error);
  }
};
HRESULT Unregister(bool machine) {
  RegistrationRemoval removal{machine};
  return yanvy::UnregisterTextService(removal);
}
}
#ifndef YANVY_SETUP_REGISTRY_FIXTURE
int wmain(int argc, wchar_t** argv) {
  HRESULT init = CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
  if (FAILED(init)) return 1;
  const std::wstring option = argc > 1 ? argv[1] : L"--status";
  const bool machine = argc > 2 && std::wstring(argv[2]) == L"--machine";
  HRESULT hr = E_INVALIDARG;
  if (option == L"--register") hr = Register(machine);
  else if (option == L"--unregister") hr = Unregister(machine);
  else if (option == L"--stop-previous-brokers") {
    // Close the superseded per-user brokers after a successful upgrade so
    // their notification icons cannot coexist with the new one. Do not run
    // executables from the user-selected previous directory as administrator.
    const auto sid = yanvy::UserSid();
    DWORD session = 0;
    if (sid.empty() || !ProcessIdToSessionId(GetCurrentProcessId(), &session)) hr = E_FAIL;
    else {
      hr = S_OK;
      for (const auto version : {L"v3", L"v4", L"v5", L"v6"}) {
        const auto name = std::wstring(L"Local\\Stop-YANVY-") + version + L"-" + sid + L"-" + std::to_wstring(session);
        HANDLE event = OpenEventW(EVENT_MODIFY_STATE, FALSE, name.c_str());
        if (event) { if (!SetEvent(event)) hr = HRESULT_FROM_WIN32(GetLastError()); CloseHandle(event); }
        else if (GetLastError() != ERROR_FILE_NOT_FOUND) hr = HRESULT_FROM_WIN32(GetLastError());
      }
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
      auto factory_function = reinterpret_cast<GetFactory>(GetProcAddress(library,"DllGetClassObject"));
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
      auto can_unload = reinterpret_cast<CanUnload>(GetProcAddress(library,"DllCanUnloadNow"));
      if (SUCCEEDED(hr) && (!can_unload || can_unload() != S_OK)) hr = E_UNEXPECTED;
      if (SUCCEEDED(hr)) FreeLibrary(library);
    }
  }
  std::cout << "result=0x" << std::hex << static_cast<unsigned long>(hr) << '\n';
  CoUninitialize();
  return SUCCEEDED(hr) ? 0 : 1;
}
#endif
