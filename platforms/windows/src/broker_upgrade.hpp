// SPDX-License-Identifier: BSD-3-Clause
#pragma once
#include "common.hpp"
#include "machine_store.hpp"
#include <tlhelp32.h>

namespace yanvy {
struct UpgradeBrokerSnapshot { DWORD pid=0;ULONGLONG created=0; };
struct UpgradeBrokerObjects { std::wstring mutex,stop,pipe; };
inline UpgradeBrokerObjects CurrentUpgradeBrokerObjects(){
  return {ObjectName(L"Local\\Broker-"),ObjectName(L"Local\\Stop-"),PipeName()};
}
struct UpgradeHandle {
  HANDLE value=nullptr;
  explicit UpgradeHandle(HANDLE handle):value(handle){}
  ~UpgradeHandle(){if(value&&value!=INVALID_HANDLE_VALUE)CloseHandle(value);}
  UpgradeHandle(const UpgradeHandle&)=delete;
};
inline ULONGLONG UpgradeProcessCreated(HANDLE process){
  FILETIME created{},exit{},kernel{},user{};
  if(!GetProcessTimes(process,&created,&exit,&kernel,&user))return 0;
  return (ULONGLONG(created.dwHighDateTime)<<32)|created.dwLowDateTime;
}
inline bool UpgradeProcessMatches(HANDLE process,DWORD pid,const std::filesystem::path& executable,const std::wstring& sid,DWORD session){
  DWORD actual_session=0;if(!ProcessIdToSessionId(pid,&actual_session)||actual_session!=session)return false;
  wchar_t image[32768]{};DWORD size=DWORD(std::size(image));
  if(!QueryFullProcessImageNameW(process,0,image,&size)||!SameMachinePath(image,executable))return false;
  HANDLE raw_token=nullptr;if(!OpenProcessToken(process,TOKEN_QUERY,&raw_token))return false;
  UpgradeHandle token(raw_token);DWORD bytes=0;GetTokenInformation(token.value,TokenUser,nullptr,0,&bytes);
  if(!bytes)return false;
  std::vector<BYTE> data(bytes);
  if(!GetTokenInformation(token.value,TokenUser,data.data(),bytes,&bytes))return false;
  LPWSTR value=nullptr;if(!ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(data.data())->User.Sid,&value))return false;
  const bool matches=sid==value;LocalFree(value);return matches;
}
inline HRESULT CaptureUpgradeBroker(const std::filesystem::path& previous,const std::filesystem::path& current,UpgradeBrokerSnapshot& snapshot){
  snapshot={};
  if(previous.empty()||SameMachinePath(previous,current))return S_FALSE;
  if(!previous.is_absolute()||previous.lexically_normal()!=previous)return E_INVALIDARG;
  const auto expected=previous/L"YANVY-Server.exe";const auto sid=UserSid();DWORD session=0;
  if(sid.empty()||!ProcessIdToSessionId(GetCurrentProcessId(),&session))return E_FAIL;
  UpgradeHandle list(CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0));
  if(list.value==INVALID_HANDLE_VALUE)return HRESULT_FROM_WIN32(GetLastError());
  PROCESSENTRY32W entry{};entry.dwSize=sizeof(entry);
  if(!Process32FirstW(list.value,&entry))return HRESULT_FROM_WIN32(GetLastError());
  do{
    if(_wcsicmp(entry.szExeFile,L"YANVY-Server.exe")!=0)continue;
    UpgradeHandle process(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE,FALSE,entry.th32ProcessID));
    if(!process.value||!UpgradeProcessMatches(process.value,entry.th32ProcessID,expected,sid,session))continue;
    const auto created=UpgradeProcessCreated(process.value);if(!created)continue;
    if(snapshot.pid)return HRESULT_FROM_WIN32(ERROR_DUP_NAME);
    snapshot={entry.th32ProcessID,created};
  }while(Process32NextW(list.value,&entry));
  return snapshot.pid?S_OK:S_FALSE;
}
#ifdef YANVY_BROKER_UPGRADE_FIXTURE
// Explicit in-process fixture hook; absent from every shipped binary.
inline void(*upgrade_fixture_after_exit)()=nullptr;
#endif
inline HRESULT StopCapturedUpgradeBroker(const UpgradeBrokerSnapshot& snapshot,const std::filesystem::path& previous,
    const std::filesystem::path& current,const UpgradeBrokerObjects& objects,DWORD timeout=10000){
  if(!snapshot.pid||!snapshot.created||previous.empty()||SameMachinePath(previous,current))return S_FALSE;
  if(!previous.is_absolute()||previous.lexically_normal()!=previous)return E_INVALIDARG;
  UpgradeHandle process(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE,FALSE,snapshot.pid));
  if(!process.value){const auto error=GetLastError();return error==ERROR_INVALID_PARAMETER?S_FALSE:HRESULT_FROM_WIN32(error);}
  if(WaitForSingleObject(process.value,0)==WAIT_OBJECT_0)return S_FALSE;
  const auto sid=UserSid();DWORD session=0;
  if(sid.empty()||!ProcessIdToSessionId(GetCurrentProcessId(),&session))return E_FAIL;
  if(UpgradeProcessCreated(process.value)!=snapshot.created||
      !UpgradeProcessMatches(process.value,snapshot.pid,previous/L"YANVY-Server.exe",sid,session))return E_ACCESSDENIED;
  // Keep the old singleton object alive until its process has exited. A new
  // broker refuses an existing singleton, so it cannot inherit a signaled
  // stop event or race into the interval between verification and signaling.
  UpgradeHandle barrier(OpenMutexW(SYNCHRONIZE,FALSE,objects.mutex.c_str()));
  if(!barrier.value){const auto error=GetLastError();return error==ERROR_FILE_NOT_FOUND?S_FALSE:HRESULT_FROM_WIN32(error);}
  UpgradeHandle stop(OpenEventW(EVENT_MODIFY_STATE,FALSE,objects.stop.c_str()));
  if(!stop.value)return HRESULT_FROM_WIN32(GetLastError());
  const auto deadline=GetTickCount64()+timeout;
  HANDLE pipe=INVALID_HANDLE_VALUE;
  while(GetTickCount64()<deadline){
    if(WaitForSingleObject(process.value,0)==WAIT_OBJECT_0)return S_FALSE;
    pipe=CreateFileW(objects.pipe.c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_EXISTING,FILE_FLAG_OVERLAPPED,nullptr);
    if(pipe!=INVALID_HANDLE_VALUE)break;
    const auto error=GetLastError();if(error!=ERROR_FILE_NOT_FOUND&&error!=ERROR_PIPE_BUSY)return HRESULT_FROM_WIN32(error);
    WaitForSingleObject(process.value,50);
  }
  if(pipe==INVALID_HANDLE_VALUE)return HRESULT_FROM_WIN32(WAIT_TIMEOUT);
  ULONG server_pid=0;const BOOL identified=GetNamedPipeServerProcessId(pipe,&server_pid);const auto pipe_error=GetLastError();CloseHandle(pipe);
  if(!identified)return HRESULT_FROM_WIN32(pipe_error);
  if(server_pid!=snapshot.pid||WaitForSingleObject(process.value,0)==WAIT_OBJECT_0)return S_FALSE;
  if(!SetEvent(stop.value))return HRESULT_FROM_WIN32(GetLastError());
  const auto now=GetTickCount64();const DWORD remaining=now<deadline?DWORD(deadline-now):0;
  const DWORD wait=WaitForSingleObject(process.value,remaining);
  if(wait!=WAIT_OBJECT_0)return HRESULT_FROM_WIN32(wait==WAIT_TIMEOUT?WAIT_TIMEOUT:GetLastError());
#ifdef YANVY_BROKER_UPGRADE_FIXTURE
  if(upgrade_fixture_after_exit)upgrade_fixture_after_exit();
#endif
  return S_OK;
}
inline HRESULT VerifyCurrentUpgradeBroker(const std::filesystem::path& current,const UpgradeBrokerObjects& objects,DWORD timeout=10000){
  const auto sid=UserSid();DWORD session=0;
  if(sid.empty()||!ProcessIdToSessionId(GetCurrentProcessId(),&session))return E_FAIL;
  const auto deadline=GetTickCount64()+timeout;
  while(GetTickCount64()<deadline){
    HANDLE pipe=CreateFileW(objects.pipe.c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_EXISTING,FILE_FLAG_OVERLAPPED,nullptr);
    if(pipe!=INVALID_HANDLE_VALUE){
      ULONG pid=0;const BOOL identified=GetNamedPipeServerProcessId(pipe,&pid);const DWORD error=GetLastError();CloseHandle(pipe);
      if(!identified)return HRESULT_FROM_WIN32(error);
      UpgradeHandle process(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE,FALSE,pid));
      return process.value&&UpgradeProcessMatches(process.value,pid,current/L"YANVY-Server.exe",sid,session)&&
          WaitForSingleObject(process.value,0)==WAIT_TIMEOUT?S_OK:E_ACCESSDENIED;
    }
    const DWORD error=GetLastError();if(error!=ERROR_PIPE_BUSY&&error!=ERROR_FILE_NOT_FOUND)return HRESULT_FROM_WIN32(error);
    Sleep(50);
  }
  return HRESULT_FROM_WIN32(WAIT_TIMEOUT);
}
}
