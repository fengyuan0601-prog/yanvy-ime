// SPDX-License-Identifier: BSD-3-Clause
// Isolated event test: never names a real user's broker or stops a process.
#define YANVY_SETUP_REGISTRY_FIXTURE
#include "../src/setup.cpp"
#include <array>

int wmain() {
  GUID id{}; wchar_t guid[40]{};
  if (FAILED(CoCreateGuid(&id)) || !StringFromGUID2(id,guid,40)) return 1;
  const std::wstring identity = std::wstring(L"fixture-") + guid;
  DWORD session=0;
  if (!ProcessIdToSessionId(GetCurrentProcessId(),&session)) return 2;
  std::array<HANDLE,7> events{};
  struct Close { std::array<HANDLE,7>& events; ~Close(){for(auto h:events)if(h)CloseHandle(h);} } close{events};
  for (unsigned version=1;version<=7;++version) {
    const auto name=std::wstring(L"Local\\Stop-YANVY-v")+std::to_wstring(version)+L"-"+identity+L"-"+std::to_wstring(session);
    events[version-1]=CreateEventW(nullptr,TRUE,FALSE,name.c_str());
    if(!events[version-1] || GetLastError()==ERROR_ALREADY_EXISTS) return 3;
  }
  if(FAILED(StopPreviousBrokers(identity,session))) return 4;
  for(unsigned version=1;version<=7;++version) {
    const bool signaled=WaitForSingleObject(events[version-1],0)==WAIT_OBJECT_0;
    const bool expected=version>=2&&version<=6;
    if(signaled!=expected){std::cerr<<"Unexpected stop state for v"<<version<<'\n';return 5;}
  }
  if(FAILED(StopPreviousBrokers(identity+L"-missing",session)))return 6;
  std::cout<<"Legacy v2-v6 stop events and current v7 isolation PASS\n";
  return 0;
}
