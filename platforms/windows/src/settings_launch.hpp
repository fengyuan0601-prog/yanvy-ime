// SPDX-License-Identifier: BSD-3-Clause
#pragma once
#include "common.hpp"
#include "machine_store.hpp"
#include <cstring>

namespace yanvy {
struct SettingsToken { bool app_container=false,elevated=false;DWORD integrity=0; };
inline HRESULT ReadSettingsToken(HANDLE token,SettingsToken& result){
  DWORD size=0,container=0;TOKEN_ELEVATION elevation{};
  if(!GetTokenInformation(token,TokenIsAppContainer,&container,sizeof(container),&size)||
      !GetTokenInformation(token,TokenElevation,&elevation,sizeof(elevation),&size))return HRESULT_FROM_WIN32(GetLastError());
  GetTokenInformation(token,TokenIntegrityLevel,nullptr,0,&size);std::vector<BYTE> buffer(size);
  if(buffer.empty()||!GetTokenInformation(token,TokenIntegrityLevel,buffer.data(),size,&size))return HRESULT_FROM_WIN32(GetLastError());
  PSID sid=reinterpret_cast<TOKEN_MANDATORY_LABEL*>(buffer.data())->Label.Sid;
  if(!IsValidSid(sid)||!*GetSidSubAuthorityCount(sid))return E_ACCESSDENIED;
  result={container!=0,elevation.TokenIsElevated!=0,*GetSidSubAuthority(sid,*GetSidSubAuthorityCount(sid)-1)};return S_OK;
}
inline bool OrdinarySettingsToken(const SettingsToken& value){
  return !value.app_container&&!value.elevated&&value.integrity>=SECURITY_MANDATORY_MEDIUM_RID&&value.integrity<SECURITY_MANDATORY_HIGH_RID;
}
inline HRESULT CurrentSettingsToken(SettingsToken& result){
  HANDLE token=nullptr;if(!OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY,&token))return HRESULT_FROM_WIN32(GetLastError());
  const HRESULT hr=ReadSettingsToken(token,result);CloseHandle(token);return hr;
}
inline HRESULT ValidateSettingsRoot(const std::filesystem::path& root){
  if(!VersionDirectoryName(root.filename().wstring()))return E_ACCESSDENIED;
  return ValidateMachineRuntime(root);
}
inline HRESULT LaunchFixedSettings(const std::filesystem::path& root){
#if defined(YANVY_TSF_HOST_FIXTURE) || (defined(YANVY_BROKER_FIXTURE) && !defined(YANVY_SETTINGS_LAUNCH_FIXTURE))
  (void)root;return E_NOTIMPL; // General IPC/host fixtures never open production UI.
#else
  SettingsToken token;HRESULT hr=CurrentSettingsToken(token);if(FAILED(hr))return hr;
  if(!OrdinarySettingsToken(token))return E_ACCESSDENIED;
  hr=ValidateSettingsRoot(root);if(FAILED(hr))return hr;
  const auto executable=root/L"YANVY-Settings.exe";DWORD error=ERROR_SUCCESS;
  return StartUserProcess(executable,L"\""+executable.wstring()+L"\"",root,&error)?S_OK:HRESULT_FROM_WIN32(error);
#endif
}
inline Response SettingsLaunchResponse(const Request& request,const std::filesystem::path& root) noexcept {
  // An action reply has no engine snapshot, owner, or arbitrary launch input.
  Response response{};response.last_page=0;response.ok=1;
  try{
    const GUID empty{};
    const HRESULT result=request.op==Op::OpenSettings&&!request.key&&!request.modifiers&&!request.schema&&IsEqualGUID(request.owner,empty)?
        LaunchFixedSettings(root):E_INVALIDARG;
    response.handled=static_cast<uint32_t>(result);
  }catch(...){response.handled=static_cast<uint32_t>(E_FAIL);}
  return response;
}
inline HRESULT VerifySettingsPipeServer(HANDLE pipe){
  ULONG pid=0;if(!GetNamedPipeServerProcessId(pipe,&pid)||!pid)return E_ACCESSDENIED;
  DWORD server_session=0,own_session=0;
  if(!ProcessIdToSessionId(pid,&server_session)||!ProcessIdToSessionId(GetCurrentProcessId(),&own_session)||server_session!=own_session)return E_ACCESSDENIED;
  HANDLE process=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid);if(!process)return HRESULT_FROM_WIN32(GetLastError());
  struct ProcessGuard{HANDLE value;~ProcessGuard(){CloseHandle(value);}} process_guard{process};
  wchar_t image[32768]{};DWORD length=DWORD(std::size(image));
  if(!QueryFullProcessImageNameW(process,0,image,&length))return HRESULT_FROM_WIN32(GetLastError());
  const std::filesystem::path executable(image);
  if(_wcsicmp(executable.filename().c_str(),L"YANVY-Server.exe"))return E_ACCESSDENIED;
  HRESULT hr=ValidateSettingsRoot(executable.parent_path());if(FAILED(hr))return hr;
  HANDLE token=nullptr;if(!OpenProcessToken(process,TOKEN_QUERY,&token))return HRESULT_FROM_WIN32(GetLastError());
  struct TokenGuard{HANDLE value;~TokenGuard(){CloseHandle(value);}} token_guard{token};
  SettingsToken context;hr=ReadSettingsToken(token,context);if(FAILED(hr)||!OrdinarySettingsToken(context))return FAILED(hr)?hr:E_ACCESSDENIED;
  DWORD size=0;GetTokenInformation(token,TokenUser,nullptr,0,&size);std::vector<BYTE> user(size);
  if(user.empty()||!GetTokenInformation(token,TokenUser,user.data(),size,&size))return E_ACCESSDENIED;
  LPWSTR sid=nullptr;if(!ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(user.data())->User.Sid,&sid))return E_ACCESSDENIED;
  const bool same=UserSid()==sid;LocalFree(sid);return same?S_OK:E_ACCESSDENIED;
}
inline HRESULT RequestSettingsFromBroker(bool& unavailable){
  unavailable=false;HANDLE pipe=INVALID_HANDLE_VALUE;
  for(unsigned attempt=0;attempt<4;++attempt){
    pipe=CreateFileW(PipeName().c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_EXISTING,
        FILE_FLAG_OVERLAPPED|SECURITY_SQOS_PRESENT|SECURITY_IDENTIFICATION,nullptr);
    if(pipe!=INVALID_HANDLE_VALUE)break;
    const DWORD error=GetLastError();
    if(error==ERROR_FILE_NOT_FOUND){unavailable=true;return HRESULT_FROM_WIN32(error);}
    if(error!=ERROR_PIPE_BUSY)return HRESULT_FROM_WIN32(error);
    if(attempt<3)Sleep(10);
  }
  if(pipe==INVALID_HANDLE_VALUE)return HRESULT_FROM_WIN32(ERROR_PIPE_BUSY);
  struct PipeGuard{HANDLE value;~PipeGuard(){CloseHandle(value);}} guard{pipe};
  // Verify THIS connected handle before writing. Never verify one pipe and
  // reconnect to another process; an untrusted endpoint cannot trigger fallback.
  HRESULT hr=VerifySettingsPipeServer(pipe);if(FAILED(hr))return hr;
  DWORD mode=PIPE_READMODE_MESSAGE;if(!SetNamedPipeHandleState(pipe,&mode,nullptr,nullptr))return HRESULT_FROM_WIN32(GetLastError());
  Request request;request.op=Op::OpenSettings;Response response;
  if(!Transfer(pipe,&request,sizeof(request),true)||!Transfer(pipe,&response,sizeof(response),false))return HRESULT_FROM_WIN32(ERROR_TIMEOUT);
  BYTE ack=1;Transfer(pipe,&ack,sizeof(ack),true);
  if(response.magic!=kMagic||response.version!=kProtocol||!response.ok)return E_NOTIMPL;
  Response expected{};expected.ok=1;expected.last_page=0;expected.handled=response.handled;
  if(memcmp(&expected,&response,sizeof(response)))return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
  return static_cast<HRESULT>(response.handled);
}
inline HRESULT OpenSettings(const std::filesystem::path& root) noexcept {
#ifdef YANVY_TSF_HOST_FIXTURE
  (void)root;return S_OK;
#else
  try{
    SettingsToken token;HRESULT hr=CurrentSettingsToken(token);if(FAILED(hr))return hr;
    if(token.app_container)return E_ACCESSDENIED; // No AppContainer escape or ACL relaxation.
    bool unavailable=false;hr=RequestSettingsFromBroker(unavailable);
    // A sent request may already have opened the window. Never retry/fallback
    // after a reply, timeout, access denial, or failed endpoint verification.
    if(!unavailable||!OrdinarySettingsToken(token))return hr;
    return LaunchFixedSettings(root);
  }catch(...){return E_FAIL;}
#endif
}
}
