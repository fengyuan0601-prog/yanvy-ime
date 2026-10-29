// SPDX-License-Identifier: BSD-3-Clause
// All endpoints, protected-root injections and children stay in a fresh fixture.
#define YANVY_BROKER_FIXTURE
#define YANVY_SETTINGS_LAUNCH_FIXTURE
#define YANVY_MACHINE_STORE_FIXTURE
#include "common.hpp"
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
unsigned checks=0;
void Check(bool ok,const char* message){++checks;if(!ok)throw std::runtime_error(message);}
void Context(const std::filesystem::path& root,const std::wstring& suffix){
  yanvy::machine_store_fixture_root=root.parent_path();yanvy::machine_store_fixture_owner_sid=yanvy::UserSid();yanvy::test_object_suffix=suffix;
}
HRESULT SetDacl(const std::filesystem::path& path,const std::wstring& extra=L""){
  PSECURITY_DESCRIPTOR sd=nullptr;const auto sddl=L"D:P(A;OICI;FA;;;"+yanvy::UserSid()+L")(A;OICI;GRGX;;;BU)"+extra;
  if(!ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(),SDDL_REVISION_1,&sd,nullptr))return HRESULT_FROM_WIN32(GetLastError());
  BOOL present=FALSE,defaulted=FALSE;PACL acl=nullptr;GetSecurityDescriptorDacl(sd,&present,&acl,&defaulted);
  const DWORD result=SetNamedSecurityInfoW(const_cast<wchar_t*>(path.c_str()),SE_FILE_OBJECT,DACL_SECURITY_INFORMATION|PROTECTED_DACL_SECURITY_INFORMATION,nullptr,nullptr,acl,nullptr);
  LocalFree(sd);return HRESULT_FROM_WIN32(result);
}
std::wstring StopName(const std::wstring& suffix){return L"Local\\YANVY.SettingsFixture.Stop-"+suffix;}
int Serve(const std::filesystem::path& root,const std::wstring& suffix,bool delay=false){
  Context(root,suffix);HANDLE stop=CreateEventW(nullptr,TRUE,FALSE,StopName(suffix).c_str());if(!stop)return 2;
  const std::wstring sddl=L"D:P(A;;GA;;;SY)(A;;GA;;;"+yanvy::UserSid()+L")S:(ML;;NW;;;LW)";
  PSECURITY_DESCRIPTOR sd=nullptr;if(!ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(),SDDL_REVISION_1,&sd,nullptr)){CloseHandle(stop);return 3;}
  SECURITY_ATTRIBUTES attributes{sizeof(attributes),sd,FALSE};
  while(WaitForSingleObject(stop,0)!=WAIT_OBJECT_0){
    HANDLE pipe=CreateNamedPipeW(yanvy::PipeName().c_str(),PIPE_ACCESS_DUPLEX|FILE_FLAG_OVERLAPPED|FILE_FLAG_FIRST_PIPE_INSTANCE,
        PIPE_TYPE_MESSAGE|PIPE_READMODE_MESSAGE|PIPE_WAIT|PIPE_REJECT_REMOTE_CLIENTS,1,sizeof(yanvy::Response),sizeof(yanvy::Request),500,&attributes);
    if(pipe==INVALID_HANDLE_VALUE){LocalFree(sd);CloseHandle(stop);return 4;}
    OVERLAPPED overlapped{};overlapped.hEvent=CreateEventW(nullptr,TRUE,FALSE,nullptr);
    BOOL connected=ConnectNamedPipe(pipe,&overlapped);const DWORD error=GetLastError();
    if(!connected&&error==ERROR_PIPE_CONNECTED)connected=TRUE;
    if(!connected&&error==ERROR_IO_PENDING){HANDLE events[]{stop,overlapped.hEvent};connected=WaitForMultipleObjects(2,events,FALSE,INFINITE)==WAIT_OBJECT_0+1;
      if(!connected){CancelIoEx(pipe,&overlapped);DWORD ignored=0;GetOverlappedResult(pipe,&overlapped,&ignored,TRUE);}}
    CloseHandle(overlapped.hEvent);
    if(connected){yanvy::Request request;yanvy::Response response;
      if(yanvy::Transfer(pipe,&request,sizeof(request),false)){
        if(request.magic==yanvy::kMagic&&request.version==yanvy::kProtocol&&request.op==yanvy::Op::OpenSettings)response=yanvy::SettingsLaunchResponse(request,yanvy::ModuleDir());
        if(delay)Sleep(800);
        if(yanvy::Transfer(pipe,&response,sizeof(response),true)){BYTE ack=0;yanvy::Transfer(pipe,&ack,sizeof(ack),false);}}
    }
    DisconnectNamedPipe(pipe);CloseHandle(pipe);
  }
  LocalFree(sd);CloseHandle(stop);return 0;
}
struct Child {
  HANDLE process=nullptr,thread=nullptr;std::wstring stop_name;
  Child()=default;Child(const Child&)=delete;
  ~Child(){if(process){if(!stop_name.empty()){HANDLE stop=OpenEventW(EVENT_MODIFY_STATE,FALSE,stop_name.c_str());if(stop){SetEvent(stop);CloseHandle(stop);}}
      if(WaitForSingleObject(process,10000)!=WAIT_OBJECT_0)TerminateProcess(process,99);
      CloseHandle(process);}if(thread)CloseHandle(thread);}
  bool Start(const std::filesystem::path& exe,const std::wstring& args,bool suspended=false){
    std::wstring command=L"\""+exe.wstring()+L"\" "+args;STARTUPINFOW startup{sizeof(startup)};PROCESS_INFORMATION info{};
    if(!CreateProcessW(exe.c_str(),command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW|(suspended?CREATE_SUSPENDED:0),nullptr,exe.parent_path().c_str(),&startup,&info))return false;
    process=info.hProcess;thread=info.hThread;return true;
  }
};
bool Ready(){for(unsigned i=0;i<100;++i){if(WaitNamedPipeW(yanvy::PipeName().c_str(),20))return true;Sleep(10);}return false;}
unsigned Launches(const std::filesystem::path& root){std::ifstream input(root/L"dummy-settings-launches.txt");unsigned count=0;std::string line;while(std::getline(input,line))++count;return count;}
bool WaitLaunches(const std::filesystem::path& root,unsigned count){for(unsigned i=0;i<100;++i){if(Launches(root)==count)return true;Sleep(10);}return false;}
int Restricted(const std::filesystem::path& root,const std::wstring& suffix){
  Context(root,suffix);DWORD error=0;const auto exe=root/L"YANVY-Settings.exe";
  if(yanvy::StartUserProcess(exe,L"\""+exe.wstring()+L"\"",root,&error)||!error)return 1;
  return yanvy::OpenSettings(root)==S_OK?0:2;
}
}
int wmain(int argc,wchar_t** argv){
  try{
    if(argc==1){ // This copied, fixed-name dummy never creates a window.
      if(_wcsicmp(yanvy::ModuleDir().filename().c_str(),L"0.1.4-fixture"))return 3;
      std::ofstream(yanvy::ModuleDir()/L"dummy-settings-launches.txt",std::ios::app)<<"fixed settings action\n";return 0;
    }
    if(argc==4&&std::wstring_view(argv[1])==L"--server")return Serve(argv[2],argv[3]);
    if(argc==4&&std::wstring_view(argv[1])==L"--delay-server")return Serve(argv[2],argv[3],true);
    if(argc==4&&std::wstring_view(argv[1])==L"--restricted-client")return Restricted(argv[2],argv[3]);
    if(argc!=2)return 1;
    const std::filesystem::path base=argv[1];Check(base.is_absolute()&&base.lexically_normal()==base&&!std::filesystem::exists(base),"fresh explicit fixture directory");
    yanvy::SettingsToken token;Check(SUCCEEDED(yanvy::CurrentSettingsToken(token)),"inspect fixture token");
    if(!yanvy::OrdinarySettingsToken(token)){std::cout<<"SKIP settings launch integration: requires medium interactive token\n";return 0;}
    std::filesystem::create_directories(base/L"Program Files");const auto root=base/L"Program Files"/L"YANVY"/L"0.1.4-fixture";
    GUID guid{};Check(SUCCEEDED(CoCreateGuid(&guid)),"random test namespace");wchar_t unique[40]{};StringFromGUID2(guid,unique,40);
    std::wstring suffix;for(wchar_t c:std::wstring(unique))if(c!=L'{'&&c!=L'}'&&c!=L'-')suffix+=wchar_t(towlower(c));Context(root,suffix);
    Check(SUCCEEDED(SetDacl(base/L"Program Files"))&&SUCCEEDED(yanvy::PrepareMachineRuntime(root)),"isolated protected runtime");
    wchar_t self[32768]{};GetModuleFileNameW(nullptr,self,DWORD(std::size(self)));
    std::filesystem::copy_file(self,root/L"YANVY-Server.exe");std::filesystem::copy_file(self,root/L"YANVY-Settings.exe");
    Check(SUCCEEDED(yanvy::ValidateSettingsRoot(root)),"copied fixed executables inherit protected ACL");
    DWORD launch_error=0;Check(!yanvy::StartUserProcess(root/L"missing.exe",L"missing",root,&launch_error)&&launch_error==ERROR_FILE_NOT_FOUND,"launch preserves Win32 error");
    Check(yanvy::OpenSettings(root)==S_OK&&WaitLaunches(root,1),"missing broker permits one ordinary fixed-path fallback");
    Child server;server.stop_name=StopName(suffix);Check(server.Start(root/L"YANVY-Server.exe",L"--server \""+root.wstring()+L"\" "+suffix)&&Ready(),"isolated fixed-name broker ready");
    Check(yanvy::OpenSettings(root)==S_OK&&WaitLaunches(root,2),"verified same-user/session pipe launches fixed dummy");
    yanvy::Request request;request.op=yanvy::Op::OpenSettings;request.key=65;yanvy::Response response;
    Check(yanvy::Call(request,response)&&static_cast<HRESULT>(response.handled)==E_INVALIDARG,"extra request fields cannot choose command or arguments");
    yanvy::Response expected{};expected.ok=1;expected.last_page=0;expected.handled=uint32_t(E_INVALIDARG);
    Check(!memcmp(&response,&expected,sizeof(response))&&Launches(root)==2,"failed action response contains no input data and launches nothing");
    Child restricted;Check(restricted.Start(self,L"--restricted-client \""+root.wstring()+L"\" "+suffix,true),"suspended restricted-host fixture child");
    HANDLE job=CreateJobObjectW(nullptr,nullptr);Check(job!=nullptr,"isolated child-process-limit job");
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};limits.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_ACTIVE_PROCESS;limits.BasicLimitInformation.ActiveProcessLimit=1;
    Check(SetInformationJobObject(job,JobObjectExtendedLimitInformation,&limits,sizeof(limits))&&AssignProcessToJobObject(job,restricted.process),"deny restricted host child creation");
    ResumeThread(restricted.thread);Check(WaitForSingleObject(restricted.process,15000)==WAIT_OBJECT_0,"restricted host returns boundedly");DWORD code=1;GetExitCodeProcess(restricted.process,&code);CloseHandle(job);
    Check(code==0&&WaitLaunches(root,3),"child-process-denied desktop host opens settings through ordinary broker");
    Check(SUCCEEDED(SetDacl(root/L"YANVY-Settings.exe",L"(A;;FW;;;WD)")),"inject untrusted runtime file ACL");
    Check(FAILED(yanvy::OpenSettings(root))&&Launches(root)==3,"untrusted connected endpoint rejected without direct fallback");
    Check(SUCCEEDED(SetDacl(root/L"YANVY-Settings.exe")),"restore own fixture file ACL");
    Check(DeleteFileW((root/L"YANVY-Settings.exe").c_str())!=FALSE,"remove own dummy executable");
    Check(yanvy::OpenSettings(root)==HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND)&&Launches(root)==3,"broker launch failure propagates once without fallback");
    request={};request.op=static_cast<yanvy::Op>(99);response={};Check(!yanvy::Call(request,response)&&Launches(root)==3,"unknown opcode does not launch");
    std::filesystem::copy_file(self,root/L"YANVY-Settings.exe");std::filesystem::copy_file(self,root/L"Other-Server.exe");
    const auto spoof_suffix=suffix+L"-spoof";Context(root,spoof_suffix);Child spoof;spoof.stop_name=StopName(spoof_suffix);
    Check(spoof.Start(root/L"Other-Server.exe",L"--server \""+root.wstring()+L"\" "+spoof_suffix)&&Ready(),"isolated wrong-image endpoint ready");
    Check(yanvy::OpenSettings(root)==E_ACCESSDENIED&&Launches(root)==3,"same-user wrong-image pipe rejected without fallback");
    const auto delay_suffix=suffix+L"-delayed";Context(root,delay_suffix);Child delayed;delayed.stop_name=StopName(delay_suffix);
    Check(delayed.Start(root/L"YANVY-Server.exe",L"--delay-server \""+root.wstring()+L"\" "+delay_suffix)&&Ready(),"isolated delayed-reply endpoint ready");
    Check(yanvy::OpenSettings(root)==HRESULT_FROM_WIN32(ERROR_TIMEOUT)&&WaitLaunches(root,4),"sent request timeout never directly opens a second settings process");
    Sleep(400);Check(Launches(root)==4,"late response cannot repeat fixed action");
    Check(!yanvy::OrdinarySettingsToken({true,false,SECURITY_MANDATORY_MEDIUM_RID})&&!yanvy::OrdinarySettingsToken({false,true,SECURITY_MANDATORY_HIGH_RID})&&!yanvy::OrdinarySettingsToken({false,false,SECURITY_MANDATORY_LOW_RID}),"server must be ordinary medium non-AppContainer token");
    std::cout<<"Settings launch integration PASS ("<<checks<<" checks; no production UI/registration)\n";return 0;
  }catch(const std::exception& error){std::cerr<<"FAIL "<<error.what()<<'\n';return 1;}
}
