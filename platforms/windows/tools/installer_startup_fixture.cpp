// SPDX-License-Identifier: BSD-3-Clause
// No product registration, UAC prompt or data extraction. The only child is
// this fixture, launched through Explorer and checked using an exclusive pipe.
#include "../src/installer_startup.hpp"
#include <shellapi.h>
#include <string>
#include <iostream>

namespace {
std::wstring Pipe(){DWORD session=0;ProcessIdToSessionId(GetCurrentProcessId(),&session);return L"\\\\.\\pipe\\YANVY.InstallerStartup.Fixture."+std::to_wstring(session);}
}
int wmain(){
  using namespace yanvy;
  int argc=0;auto argv=CommandLineToArgvW(GetCommandLineW(),&argc);
  const bool child=argv&&argc==2&&std::wstring(argv[1])==L"--desktop-ui";
  const bool policy_only=argv&&argc==2&&std::wstring(argv[1])==L"--policy-only";if(argv)LocalFree(argv);
  if(child){
    InstallerToken token;const HRESULT read=ReadInstallerToken(GetCurrentProcess(),token);
    const DWORD result=FAILED(read)?DWORD(read):OrdinaryInstallerToken(token)?0:ERROR_ELEVATION_REQUIRED;
    const auto pipe=Pipe();WaitNamedPipeW(pipe.c_str(),5000);
    HANDLE output=CreateFileW(pipe.c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_EXISTING,0,nullptr);
    if(output==INVALID_HANDLE_VALUE)return 2;
    DWORD written=0;
    DWORD ack=0,received=0;const bool ok=WriteFile(output,&result,sizeof(result),&written,nullptr)&&written==sizeof(result)&&
        ReadFile(output,&ack,sizeof(ack),&received,nullptr)&&received==sizeof(ack)&&ack==1;CloseHandle(output);return ok?0:3;
  }
  if(!OrdinaryInstallerToken({false,false,SECURITY_MANDATORY_MEDIUM_RID,1})||
      OrdinaryInstallerToken({true,false,SECURITY_MANDATORY_HIGH_RID,1})||
      OrdinaryInstallerToken({false,true,SECURITY_MANDATORY_MEDIUM_RID,1})||
      OrdinaryInstallerToken({false,false,SECURITY_MANDATORY_LOW_RID,1}))return 4;
  InstallerToken own;if(FAILED(ReadInstallerToken(GetCurrentProcess(),own)))return 10;
  if(policy_only){std::cout<<"Ordinary/elevated/restricted token policy and real process token query: PASS\n";return 0;}
  HANDLE pipe=CreateNamedPipeW(Pipe().c_str(),PIPE_ACCESS_DUPLEX|FILE_FLAG_FIRST_PIPE_INSTANCE|FILE_FLAG_OVERLAPPED,
      PIPE_TYPE_MESSAGE|PIPE_READMODE_MESSAGE|PIPE_WAIT|PIPE_REJECT_REMOTE_CLIENTS,1,4,4,0,nullptr);
  if(pipe==INVALID_HANDLE_VALUE)return 5;
  HANDLE event=CreateEventW(nullptr,TRUE,FALSE,nullptr);OVERLAPPED pending{};pending.hEvent=event;
  BOOL connected=ConnectNamedPipe(pipe,&pending);const DWORD error=GetLastError();
  if(!connected&&error!=ERROR_IO_PENDING&&error!=ERROR_PIPE_CONNECTED){CloseHandle(pipe);CloseHandle(event);return 6;}
  wchar_t file[32768]{};GetModuleFileNameW(nullptr,file,DWORD(std::size(file)));
  HRESULT initialized=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);if(FAILED(initialized))return 7;
  const HRESULT launched=OpenInstallerOnDesktop(file);CoUninitialize();
  if(FAILED(launched)){CancelIoEx(pipe,nullptr);CloseHandle(pipe);CloseHandle(event);std::cout<<"Explorer launch unavailable: 0x"<<std::hex<<DWORD(launched)<<"\n";return 8;}
  const bool ready=connected||error==ERROR_PIPE_CONNECTED||WaitForSingleObject(event,15000)==WAIT_OBJECT_0;
  DWORD code=1,read=0;OVERLAPPED receive{};receive.hEvent=event;ResetEvent(event);
  bool ok=false;
  if(ready){
    ULONG client_pid=0;InstallerToken client;
    const bool identified=GetNamedPipeClientProcessId(pipe,&client_pid)!=FALSE;
    HANDLE process=identified?OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,client_pid):nullptr;
    wchar_t image[32768]{};DWORD length=DWORD(std::size(image));
    const bool expected=process&&SUCCEEDED(ReadInstallerToken(process,client))&&OrdinaryInstallerToken(client)&&
        client.session==own.session&&QueryFullProcessImageNameW(process,0,image,&length)&&_wcsicmp(image,file)==0;
    if(process)CloseHandle(process);
    BOOL done=ReadFile(pipe,&code,sizeof(code),&read,&receive);if(!done&&GetLastError()==ERROR_IO_PENDING){if(WaitForSingleObject(event,5000)==WAIT_OBJECT_0)done=GetOverlappedResult(pipe,&receive,&read,FALSE);}ok=expected&&done&&read==sizeof(code)&&code==0;
  }
  if(ok){DWORD ack=1,written=0;OVERLAPPED reply{};reply.hEvent=event;ResetEvent(event);BOOL sent=WriteFile(pipe,&ack,sizeof(ack),&written,&reply);if(!sent&&GetLastError()==ERROR_IO_PENDING&&WaitForSingleObject(event,5000)==WAIT_OBJECT_0)sent=GetOverlappedResult(pipe,&reply,&written,FALSE);ok=sent&&written==sizeof(ack);}
  CancelIoEx(pipe,nullptr);DisconnectNamedPipe(pipe);CloseHandle(pipe);CloseHandle(event);
  if(!ok)return 9;
  std::cout<<"Token boundaries and real Explorer child at ordinary medium integrity: PASS\n";return 0;
}
