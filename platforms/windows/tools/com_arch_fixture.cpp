// SPDX-License-Identifier: BSD-3-Clause
// Loads the TSF DLL in this process architecture without system registration.
#include "ids.hpp"
#include "msctf_compat.hpp"
#include "protocol.hpp"
#include <wrl/client.h>
#include <iostream>

int wmain(int argc,wchar_t** argv) {
  if (argc!=2 || FAILED(CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED))) return 1;
  int result=1;
  HMODULE dll=LoadLibraryW(argv[1]);
  if (dll) {
    using GetFactory=HRESULT (WINAPI*)(REFCLSID,REFIID,void**);
    using CanUnload=HRESULT (WINAPI*)();
    auto get_factory=reinterpret_cast<GetFactory>(GetProcAddress(dll,"DllGetClassObject"));
    auto can_unload=reinterpret_cast<CanUnload>(GetProcAddress(dll,"DllCanUnloadNow"));
    if (get_factory && can_unload) {
      Microsoft::WRL::ComPtr<IClassFactory> factory;
      Microsoft::WRL::ComPtr<ITfTextInputProcessorEx> service;
      if (SUCCEEDED(get_factory(yanvy::kService,IID_IClassFactory,reinterpret_cast<void**>(factory.GetAddressOf()))) &&
          SUCCEEDED(factory->CreateInstance(nullptr,IID_ITfTextInputProcessorEx,reinterpret_cast<void**>(service.GetAddressOf())))) {
        Microsoft::WRL::ComPtr<ITfKeyEventSink> keys;
        if (SUCCEEDED(service.As(&keys))) result=0;
      }
      service.Reset();factory.Reset();
      if (can_unload()!=S_OK) result=1;
    }
    FreeLibrary(dll);
  }
  CoUninitialize();
  std::wcout << (result ? L"architecture COM test FAIL\n" : L"architecture COM test PASS\n");
  return result;
}
