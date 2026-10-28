// SPDX-License-Identifier: BSD-3-Clause
// Real, private TSF thread manager and compartment; no installed TIP, input
// injection, desktop focus changes, broker or document content involved.
#include "input_mode.hpp"
#include <iostream>
#include <vector>

using Microsoft::WRL::ComPtr;
int wmain() {
  if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED))) return 1;
  int result = 1;
  {
    ComPtr<ITfThreadMgr> manager;
    TfClientId client = TF_CLIENTID_NULL;
    if (SUCCEEDED(CoCreateInstance(CLSID_TF_ThreadMgr,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&manager))) &&
        SUCCEEDED(manager->Activate(&client))) {
      ComPtr<yanvy::InputModeCompartment> mode;
      mode.Attach(new yanvy::InputModeCompartment);
      std::vector<bool> seen;
      if (SUCCEEDED(mode->Attach(manager.Get(),client,[&](bool open) { seen.push_back(open); }))) {
        ComPtr<ITfCompartmentMgr> compartments;
        ComPtr<ITfCompartment> external;
        const bool connected = SUCCEEDED(manager.As(&compartments)) &&
          SUCCEEDED(compartments->GetCompartment(GUID_COMPARTMENT_KEYBOARD_OPENCLOSE,&external));
        auto system_open = [&]() {
          VARIANT value; VariantInit(&value); value.vt = VT_I4; value.lVal = 1;
          return connected && SUCCEEDED(external->SetValue(client,&value));
        };
        bool open = false;
        const bool ok = SUCCEEDED(mode->SetOpen(true)) && mode->Read(open) && open &&
          SUCCEEDED(mode->SetOpen(false)) && mode->Read(open) && !open &&
          system_open() && mode->Read(open) && open &&
          SUCCEEDED(mode->SetOpen(false)) && mode->Read(open) && !open &&
          system_open() && mode->Read(open) && open &&
          seen == std::vector<bool>({true,false,true,false,true});
        mode->Detach();
        result = ok && FAILED(mode->SetOpen(false)) ? 0 : 1;
      }
      mode.Reset();
      manager->Deactivate();
    }
  }
  CoUninitialize();
  std::cout << (result == 0 ? "private TSF mode round trips PASS\n" : "private TSF mode round trips FAIL\n");
  return result;
}
