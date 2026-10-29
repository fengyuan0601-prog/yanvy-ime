// SPDX-License-Identifier: GPL-3.0-or-later
// Isolated fault injection for the production removal transaction. No TSF
// registration, registry access, process control or real user input.
#include "registration_flow.hpp"
#include <iostream>
#include <stdexcept>
#include <array>
#include <objbase.h>

namespace {
struct InstalledState {
  bool profile = true, categories = true, com = true, startup = true;
  int fail_at = 0;
  HRESULT Remove(bool& entry, int step) {
    if (fail_at == step) return E_ACCESSDENIED;
    entry = false;
    return S_OK;
  }
  HRESULT RemoveProfile() { return Remove(profile,1); }
  HRESULT RemoveCategories() { return Remove(categories,2); }
  HRESULT RemoveComServer() { return Remove(com,3); }
  HRESULT RemoveStartup() { return Remove(startup,4); }
};
void Require(bool value, const char* detail) {
  if (!value) throw std::runtime_error(detail);
}
struct PreviousStates {
  std::array<InstalledState,std::size(yanvy::kPreviousTextServices)> entries{};
  bool current=true,unrelated=true;int query_failure=-1,queries=0,removals=0;
  size_t Index(const yanvy::PreviousTextService& identity){
    for(size_t index=0;index<std::size(yanvy::kPreviousTextServices);++index){
      const auto& known=yanvy::kPreviousTextServices[index];
      if(*identity.service==*known.service&&*identity.profile==*known.profile&&wcscmp(identity.clsid,known.clsid)==0)return index;
    }
    throw std::runtime_error("retirement requested an unknown identity");
  }
  HRESULT Present(const yanvy::PreviousTextService& identity,bool& present){
    const auto index=Index(identity);++queries;if(int(index)==query_failure)return E_ACCESSDENIED;
    present=entries[index].profile;return S_OK;
  }
  HRESULT Remove(const yanvy::PreviousTextService& identity){++removals;return yanvy::UnregisterTextService(entries[Index(identity)]);}
};
}
int wmain() {
  try {
    constexpr size_t generations=std::size(yanvy::kPreviousTextServices);
    Require(generations==4,"all four previous generations must be listed");
    for(const auto& previous:yanvy::kPreviousTextServices){
      Require(*previous.service!=yanvy::kService&&*previous.profile!=yanvy::kProfile,"current identity is in retirement list");
      GUID parsed{};Require(SUCCEEDED(CLSIDFromString(previous.clsid,&parsed))&&parsed==*previous.service,"previous CLSID text differs from binary GUID");
    }
    Require(yanvy::kPreviousService!=yanvy::kLegacyService,"previous release and rc identities overlap");
    Require(yanvy::kPreviousNativeService!=yanvy::kPreviousService&&yanvy::kPreviousNativeService!=yanvy::kLegacyService,"native preview identity overlaps an older release");
    GUID current{};Require(SUCCEEDED(CLSIDFromString(yanvy::kClsid,&current))&&current==yanvy::kService,"current CLSID text differs from binary GUID");
    for(unsigned combination=0;combination<(1u<<generations);++combination){
      PreviousStates states;for(size_t index=0;index<generations;++index)states.entries[index].profile=(combination&(1u<<index))!=0;
      bool present=false;Require(yanvy::PreviousTextServicePresent(states,present)==S_OK&&present==(combination!=0),"rollback presence missed a previous generation");
      Require(states.removals==0&&states.current&&states.unrelated,"read-only presence changed registration");
    }
    {
      PreviousStates states;states.entries[0].profile=false;states.query_failure=1;bool present=false;
      Require(yanvy::PreviousTextServicePresent(states,present)==E_ACCESSDENIED,"legacy query error was hidden");
      Require(states.removals==0,"failed query modified a previous profile");
    }
    for(size_t failed=0;failed<generations;++failed){
      PreviousStates states;states.entries[failed].fail_at=1;
      Require(yanvy::RetirePreviousTextServices(states)==E_ACCESSDENIED,"retirement failure was hidden");
      Require(states.entries[failed].profile&&states.entries[failed].com,"failed old profile lost its COM registration");
      Require(states.current&&states.unrelated&&states.removals==int(failed+1),"retirement crossed a failure or touched unrelated identity");
      states.entries[failed].fail_at=0;
      Require(yanvy::RetirePreviousTextServices(states)==S_OK,"previous-generation cleanup could not retry");
      for(const auto& entry:states.entries)Require(!entry.profile,"cleanup missed previous identity");
      Require(states.current&&states.unrelated,"cleanup touched current or unrelated identity");
    }
    for (int fail = 1; fail <= 4; ++fail) {
      InstalledState state; state.fail_at = fail;
      Require(yanvy::UnregisterTextService(state) == E_ACCESSDENIED,"removal error was hidden");
      Require(state.profile == (fail == 1),"profile changed after rejected removal");
      Require(state.categories == (fail <= 2),"categories removed after earlier failure");
      Require(state.com == (fail <= 3),"COM server removed after earlier failure");
      Require(state.startup,"startup removed after rejected removal");
      state.fail_at = 0;
      Require(yanvy::UnregisterTextService(state) == S_OK,"partial cleanup could not be retried");
      Require(!state.profile && !state.categories && !state.com && !state.startup,"retry left owned entries");
      Require(yanvy::UnregisterTextService(state) == S_OK,"already-absent cleanup failed");
    }
    std::cout << "registration cleanup: PASS (four previous identities, read-only rollback checks, failure preservation, retry and absent-entry cases; no system mutation)\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
