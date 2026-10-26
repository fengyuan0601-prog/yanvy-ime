// SPDX-License-Identifier: GPL-3.0-or-later
// Registration sequence adapted from WeaselTSF/Register.cpp; see provenance.
#include "common.hpp"
#include "ids.hpp"
#include <msctf.h>
#include "msctf_compat.hpp"
#include <wrl/client.h>
#include <iostream>
#include <algorithm>

using Microsoft::WRL::ComPtr;
namespace {
const GUID* const categories[] = { &GUID_TFCAT_TIP_KEYBOARD };
HRESULT SetString(HKEY root, const std::wstring& path, const wchar_t* name, const std::wstring& value) {
  HKEY key = nullptr;
  LSTATUS error = RegCreateKeyExW(root,path.c_str(),0,nullptr,0,KEY_WRITE,nullptr,&key,nullptr);
  if (error != ERROR_SUCCESS) return HRESULT_FROM_WIN32(error);
  error = RegSetValueExW(key,name,0,REG_SZ,reinterpret_cast<const BYTE*>(value.c_str()),DWORD((value.size()+1)*sizeof(wchar_t)));
  RegCloseKey(key);
  return HRESULT_FROM_WIN32(error);
}
HRESULT Register(bool machine) {
  const auto dll = yanvy::ModuleDir() / L"YANVY-TSF.dll";
  const std::wstring key = std::wstring(L"Software\\Classes\\CLSID\\") + yanvy::kClsid;
  const HKEY hive = machine ? HKEY_LOCAL_MACHINE : HKEY_CURRENT_USER;
  HRESULT hr = SetString(hive,key,nullptr,yanvy::kName);
  if (SUCCEEDED(hr)) hr = SetString(hive,key+L"\\InprocServer32",nullptr,dll.wstring());
  if (SUCCEEDED(hr)) hr = SetString(hive,key+L"\\InprocServer32",L"ThreadingModel",L"Apartment");
  if (FAILED(hr)) return hr;
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
    if (category) for (auto guid : categories) category->UnregisterCategory(yanvy::kService,*guid,yanvy::kService);
    if (profiles) profiles->UnregisterProfile(yanvy::kService,yanvy::kLanguage,yanvy::kProfile,0);
    RegDeleteTreeW(hive,key.c_str());
  }
  return hr;
}
HRESULT Unregister(bool machine) {
  ComPtr<ITfInputProcessorProfiles> profiles;
  HRESULT hr = CoCreateInstance(CLSID_TF_InputProcessorProfiles,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&profiles));
  if (SUCCEEDED(hr)) {
    profiles->EnableLanguageProfile(yanvy::kService,yanvy::kLanguage,yanvy::kProfile,FALSE);
    hr = profiles->Unregister(yanvy::kService);
  }
  ComPtr<ITfCategoryMgr> category;
  if (SUCCEEDED(CoCreateInstance(CLSID_TF_CategoryMgr,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&category))))
    for (auto guid : categories) category->UnregisterCategory(yanvy::kService,*guid,yanvy::kService);
  const std::wstring key = std::wstring(L"Software\\Classes\\CLSID\\") + yanvy::kClsid;
  RegDeleteTreeW(machine ? HKEY_LOCAL_MACHINE : HKEY_CURRENT_USER,key.c_str());
  HKEY run = nullptr;
  if (RegOpenKeyExW(HKEY_CURRENT_USER,L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",0,KEY_SET_VALUE,&run) == ERROR_SUCCESS) {
    RegDeleteValueW(run,L"YANVY-dev"); RegCloseKey(run);
  }
  return hr;
}
}
int wmain(int argc, wchar_t** argv) {
  HRESULT init = CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
  if (FAILED(init)) return 1;
  const std::wstring option = argc > 1 ? argv[1] : L"--status";
  const bool machine = argc > 2 && std::wstring(argv[2]) == L"--machine";
  HRESULT hr = E_INVALIDARG;
  if (option == L"--register") hr = Register(machine);
  else if (option == L"--unregister") hr = Unregister(machine);
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
