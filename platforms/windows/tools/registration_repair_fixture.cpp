// SPDX-License-Identifier: GPL-3.0-or-later
// Failure injection only: no COM, registry, filesystem or installed IME calls.
#include "registration_flow.hpp"
#include <iostream>
#include <stdexcept>
#include <vector>
#include <string>
namespace {
unsigned checks=0;
void Check(bool ok,const char* name){++checks;if(!ok)throw std::runtime_error(name);}
struct Operations {
  unsigned fail_step=0;
  bool com=true,profile=true,category=true;
  bool profile_cleanup_fails=false,category_cleanup_fails=false;
  std::vector<std::string> calls;
  HRESULT WriteComServer(){calls.push_back("write-com");if(fail_step==1)return E_ACCESSDENIED;com=true;return S_OK;}
  HRESULT RegisterProfile(){calls.push_back("register-profile");if(fail_step==2)return E_ACCESSDENIED;profile=true;return S_OK;}
  HRESULT RegisterCategories(){calls.push_back("register-category");if(fail_step==3)return E_ACCESSDENIED;category=true;return S_OK;}
  HRESULT RemoveProfile(){calls.push_back("remove-profile");if(profile_cleanup_fails)return E_FAIL;profile=false;return S_OK;}
  HRESULT RemoveRegisteredCategories(){calls.push_back("remove-category");if(category_cleanup_fails)return E_FAIL;category=false;return S_OK;}
  void RemoveComServer(){calls.push_back("remove-com");com=false;}
};
}
int wmain(){
  try{
    for(unsigned failure=1;failure<=3;++failure){
      Operations repair;repair.fail_step=failure;
      Check(yanvy::RegisterTextService(repair,yanvy::RegistrationMode::repair)==E_ACCESSDENIED,"repair reports original failure");
      Check(repair.com&&repair.profile&&repair.category,"repair failure preserves working registration");
      Check(repair.calls.size()==failure,"repair failure never starts rollback removal");
      repair.fail_step=0;repair.calls.clear();
      Check(yanvy::RegisterTextService(repair,yanvy::RegistrationMode::repair)==S_OK,"repair can retry");
      Check(repair.com&&repair.profile&&repair.category&&repair.calls.size()==3,"successful repair does not remove registration");
    }
    for(unsigned failure=1;failure<=3;++failure){
      Operations fresh;fresh.fail_step=failure;fresh.com=fresh.profile=fresh.category=false;
      Check(yanvy::RegisterTextService(fresh,yanvy::RegistrationMode::fresh)==E_ACCESSDENIED,"fresh install reports failure");
      Check(!fresh.com&&!fresh.profile&&!fresh.category,"fresh failure removes its partial registration");
      Check(fresh.calls.back()=="remove-com","fresh rollback keeps original cleanup order");
    }
    Operations profile_blocked;profile_blocked.fail_step=3;profile_blocked.profile_cleanup_fails=true;
    Check(FAILED(yanvy::RegisterTextService(profile_blocked,yanvy::RegistrationMode::fresh)),"profile rollback failure reported");
    Check(profile_blocked.com&&profile_blocked.profile&&profile_blocked.category&&profile_blocked.calls.back()=="remove-profile","failed profile cleanup preserves dependencies");
    Operations category_blocked;category_blocked.fail_step=3;category_blocked.category_cleanup_fails=true;
    Check(FAILED(yanvy::RegisterTextService(category_blocked,yanvy::RegistrationMode::fresh)),"category rollback failure reported");
    Check(category_blocked.com&&category_blocked.category&&!category_blocked.profile&&category_blocked.calls.back()=="remove-category","failed category cleanup preserves COM");
    Operations missing;missing.com=missing.profile=missing.category=false;
    Check(yanvy::RegisterTextService(missing,yanvy::RegistrationMode::repair)==S_OK&&missing.com&&missing.profile&&missing.category,"repair restores missing registration");
    std::cout<<"Repair registration preservation, retry and unchanged fresh rollback PASS ("<<checks<<" checks)\n";return 0;
  }catch(const std::exception& error){std::cerr<<"FAIL "<<error.what()<<'\n';return 1;}
}
