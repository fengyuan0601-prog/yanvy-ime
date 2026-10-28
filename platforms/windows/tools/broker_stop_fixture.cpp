// SPDX-License-Identifier: BSD-3-Clause
// Isolated event test: never names a real user's broker or stops a process.
#define YANVY_SETUP_REGISTRY_FIXTURE
#define YANVY_BROKER_UPGRADE_FIXTURE
#include "../src/setup.cpp"
#include <array>

namespace {
yanvy::UpgradeBrokerObjects FixtureObjects(const std::wstring& suffix){
  return {L"Local\\YANVY-Upgrade-Mutex-"+suffix,L"Local\\YANVY-Upgrade-Stop-"+suffix,L"\\\\.\\pipe\\YANVY-Upgrade-Pipe-"+suffix};
}
std::wstring ReadyName(const std::wstring& suffix){return L"Local\\YANVY-Upgrade-Ready-"+suffix;}
int FixtureBroker(const std::wstring& suffix){
  const auto names=FixtureObjects(suffix);
  yanvy::UpgradeHandle singleton(CreateMutexW(nullptr,TRUE,names.mutex.c_str()));
  if(!singleton.value||GetLastError()==ERROR_ALREADY_EXISTS)return 23;
  yanvy::UpgradeHandle stop(CreateEventW(nullptr,TRUE,FALSE,names.stop.c_str()));
  yanvy::UpgradeHandle connected(CreateEventW(nullptr,TRUE,FALSE,nullptr));
  yanvy::UpgradeHandle pipe(CreateNamedPipeW(names.pipe.c_str(),PIPE_ACCESS_DUPLEX|FILE_FLAG_OVERLAPPED|FILE_FLAG_FIRST_PIPE_INSTANCE,
      PIPE_TYPE_BYTE|PIPE_READMODE_BYTE|PIPE_WAIT,1,64,64,0,nullptr));
  if(!stop.value||!connected.value||pipe.value==INVALID_HANDLE_VALUE)return 24;
  OVERLAPPED connection{};connection.hEvent=connected.value;
  if(!ConnectNamedPipe(pipe.value,&connection)&&GetLastError()!=ERROR_IO_PENDING)return 25;
  yanvy::UpgradeHandle ready(OpenEventW(EVENT_MODIFY_STATE,FALSE,ReadyName(suffix).c_str()));
  if(!ready.value||!SetEvent(ready.value))return 26;
  const DWORD waited=WaitForSingleObject(stop.value,30000);
  CancelIoEx(pipe.value,&connection);DWORD bytes=0;GetOverlappedResult(pipe.value,&connection,&bytes,TRUE);
  return waited==WAIT_OBJECT_0?0:27;
}
struct Child {
  HANDLE process=nullptr,ready=nullptr;std::wstring suffix;
  ~Child(){
    if(process){auto names=FixtureObjects(suffix);HANDLE stop=OpenEventW(EVENT_MODIFY_STATE,FALSE,names.stop.c_str());
      if(stop){SetEvent(stop);CloseHandle(stop);}WaitForSingleObject(process,3000);CloseHandle(process);}
    if(ready)CloseHandle(ready);
  }
  bool Start(const std::filesystem::path& executable,const std::wstring& id){
    suffix=id;ready=CreateEventW(nullptr,TRUE,FALSE,ReadyName(suffix).c_str());if(!ready)return false;ResetEvent(ready);
    std::wstring command=L"\""+executable.wstring()+L"\" --fixture-broker \""+suffix+L"\"";
    STARTUPINFOW startup{sizeof(startup)};PROCESS_INFORMATION child{};
    if(!CreateProcessW(executable.c_str(),command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,executable.parent_path().c_str(),&startup,&child))return false;
    CloseHandle(child.hThread);process=child.hProcess;return WaitForSingleObject(ready,5000)==WAIT_OBJECT_0;
  }
};
yanvy::UpgradeBrokerObjects hook_objects;bool barrier_pinned=false;
void CheckPinnedBarrier(){
  HANDLE mutex=CreateMutexW(nullptr,FALSE,hook_objects.mutex.c_str());
  barrier_pinned=mutex&&GetLastError()==ERROR_ALREADY_EXISTS;if(mutex)CloseHandle(mutex);
}
int UpgradeFixture(const std::filesystem::path& root){
  if(!root.is_absolute()||root.lexically_normal()!=root||root==root.root_path()||std::filesystem::exists(root))return 31;
  const auto old_root=root/L"old",new_root=root/L"new";std::filesystem::create_directories(old_root);std::filesystem::create_directory(new_root);
  wchar_t self[32768]{};GetModuleFileNameW(nullptr,self,DWORD(std::size(self)));
  if(!CopyFileW(self,(old_root/L"YANVY-Server.exe").c_str(),TRUE)||!CopyFileW(self,(new_root/L"YANVY-Server.exe").c_str(),TRUE))return 32;
  GUID id{};wchar_t guid[40]{};if(FAILED(CoCreateGuid(&id))||!StringFromGUID2(id,guid,40))return 33;
  const std::wstring suffix=guid;const auto names=FixtureObjects(suffix);
  yanvy::UpgradeBrokerSnapshot empty;if(yanvy::CaptureUpgradeBroker(root/L"absent",new_root,empty)!=S_FALSE||empty.pid)return 34;
  Child old;if(!old.Start(old_root/L"YANVY-Server.exe",suffix))return 35;
  yanvy::UpgradeBrokerSnapshot snapshot;if(yanvy::CaptureUpgradeBroker(old_root,new_root,snapshot)!=S_OK||snapshot.pid!=GetProcessId(old.process))return 36;
  auto wrong=snapshot;++wrong.created;
  if(yanvy::StopCapturedUpgradeBroker(wrong,old_root,new_root,names,1500)!=E_ACCESSDENIED)return 37;
  if(yanvy::StopCapturedUpgradeBroker(snapshot,new_root,old_root,names,1500)!=E_ACCESSDENIED)return 38;
  if(yanvy::StopCapturedUpgradeBroker(snapshot,old_root,old_root,names,1500)!=S_FALSE)return 39;
  DWORD session=0;ProcessIdToSessionId(GetCurrentProcessId(),&session);
  if(yanvy::UpgradeProcessMatches(old.process,snapshot.pid,old_root/L"YANVY-Server.exe",L"not-this-user",session)||
      yanvy::UpgradeProcessMatches(old.process,snapshot.pid,old_root/L"YANVY-Server.exe",yanvy::UserSid(),session+1))return 40;
  if(WaitForSingleObject(old.process,0)!=WAIT_TIMEOUT)return 41;
  hook_objects=names;yanvy::upgrade_fixture_after_exit=CheckPinnedBarrier;
  if(yanvy::StopCapturedUpgradeBroker(snapshot,old_root,new_root,names,1500)!=S_OK||!barrier_pinned)return 42;
  yanvy::upgrade_fixture_after_exit=nullptr;
  if(WaitForSingleObject(old.process,0)!=WAIT_OBJECT_0)return 43;
  {
    Child different;const auto other=suffix+L"-other";
    if(!different.Start(new_root/L"YANVY-Server.exe",other))return 47;
    if(yanvy::VerifyCurrentUpgradeBroker(old_root,FixtureObjects(other),1500)!=E_ACCESSDENIED)return 48;
  }
  Child current;if(!current.Start(new_root/L"YANVY-Server.exe",suffix))return 44;
  if(yanvy::VerifyCurrentUpgradeBroker(new_root,names,1500)!=S_OK)return 49;
  if(yanvy::StopCapturedUpgradeBroker(snapshot,old_root,new_root,names,1500)!=S_FALSE)return 45;
  yanvy::UpgradeHandle new_stop(OpenEventW(SYNCHRONIZE,FALSE,names.stop.c_str()));
  if(!new_stop.value||WaitForSingleObject(new_stop.value,0)!=WAIT_TIMEOUT||WaitForSingleObject(current.process,0)!=WAIT_TIMEOUT)return 46;
  std::cout<<"same-protocol upgrade: captured old broker cooperatively exits; PID/time/path/user/session checks, pinned singleton and replacement-broker isolation PASS\n";
  return 0;
}
}

int wmain(int argc,wchar_t** argv) {
  if(argc==3&&std::wstring(argv[1])==L"--fixture-broker")return FixtureBroker(argv[2]);
  if(argc==3&&std::wstring(argv[1])==L"--upgrade")return UpgradeFixture(argv[2]);
  if(argc!=1)return 1;
  GUID id{}; wchar_t guid[40]{};
  if (FAILED(CoCreateGuid(&id)) || !StringFromGUID2(id,guid,40)) return 1;
  const std::wstring identity = std::wstring(L"fixture-") + guid;
  DWORD session=0;
  if (!ProcessIdToSessionId(GetCurrentProcessId(),&session)) return 2;
  std::array<HANDLE,8> events{};
  struct Close { std::array<HANDLE,8>& events; ~Close(){for(auto h:events)if(h)CloseHandle(h);} } close{events};
  for (unsigned version=1;version<=8;++version) {
    const auto name=std::wstring(L"Local\\Stop-YANVY-v")+std::to_wstring(version)+L"-"+identity+L"-"+std::to_wstring(session);
    events[version-1]=CreateEventW(nullptr,TRUE,FALSE,name.c_str());
    if(!events[version-1] || GetLastError()==ERROR_ALREADY_EXISTS) return 3;
  }
  if(FAILED(StopPreviousBrokers(identity,session))) return 4;
  for(unsigned version=1;version<=8;++version) {
    const bool signaled=WaitForSingleObject(events[version-1],0)==WAIT_OBJECT_0;
    const bool expected=version>=2&&version<=7;
    if(signaled!=expected){std::cerr<<"Unexpected stop state for v"<<version<<'\n';return 5;}
  }
  if(FAILED(StopPreviousBrokers(identity+L"-missing",session)))return 6;
  std::cout<<"Legacy v2-v7 stop events and current v8 isolation PASS\n";
  return 0;
}
