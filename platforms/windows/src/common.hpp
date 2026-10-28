// SPDX-License-Identifier: BSD-3-Clause
#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include "input_schemas.hpp"
#include <sddl.h>
#include <shlobj.h>
#include <string>
#include <filesystem>
#include <vector>
#include "protocol.hpp"

namespace yanvy {
inline std::wstring Wide(const std::string& s) {
  if (s.empty()) return {};
  int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), int(s.size()), nullptr, 0);
  if (!n) return {};
  std::wstring v(n, 0);
  MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), int(s.size()), v.data(), n);
  return v;
}
inline std::string Utf8(const std::wstring& s) {
  if (s.empty()) return {};
  int n = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, s.data(), int(s.size()), nullptr, 0, nullptr, nullptr);
  if (!n) return {};
  std::string v(n, 0);
  WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, s.data(), int(s.size()), v.data(), n, nullptr, nullptr);
  return v;
}
inline std::filesystem::path ModuleDir(HMODULE module = nullptr) {
  std::wstring path(32768, 0);
  DWORD n = GetModuleFileNameW(module, path.data(), DWORD(path.size()));
  path.resize(n);
  return std::filesystem::path(path).parent_path();
}
// User settings are shared across upgrades; the installed file supplies the
// initial choice and migrated defaults until the user saves a preference.
inline std::wstring UserSid();
inline bool PlainLocalPath(std::filesystem::path path) {
  if(!path.is_absolute()||path.root_name().wstring().size()!=2)return false;
  UINT drive=GetDriveTypeW(path.root_path().c_str());
  if(drive!=DRIVE_FIXED&&drive!=DRIVE_REMOVABLE)return false;
  for(;!path.empty();path=path.parent_path()){
    DWORD attributes=GetFileAttributesW(path.c_str());
    if(attributes!=INVALID_FILE_ATTRIBUTES&&(attributes&FILE_ATTRIBUTE_REPARSE_POINT))return false;
    if(path==path.parent_path())break;
  }
  return true;
}
inline bool CanCreateUserData(std::filesystem::path path) {
  while(!path.empty()&&GetFileAttributesW(path.c_str())==INVALID_FILE_ATTRIBUTES){
    auto parent=path.parent_path();if(parent==path)return false;path=parent;
  }
  if(path.empty())return false;
  HANDLE directory=CreateFileW(path.c_str(),FILE_ADD_SUBDIRECTORY,
      FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS,nullptr);
  if(directory==INVALID_HANDLE_VALUE)return false;
  CloseHandle(directory);return true;
}
inline std::filesystem::path UserDataRoot() {
  // Honor the established E: storage location on machines that have it;
  // ordinary installations without that location use Windows LocalAppData.
  const std::filesystem::path preferred=L"E:\\Codex\\Programs";
  std::error_code error;
  if(GetDriveTypeW(L"E:\\")==DRIVE_FIXED&&std::filesystem::is_directory(preferred,error)){
    auto sid=UserSid();auto data=preferred/L"YANVY-UserData"/sid;
    if(!sid.empty()&&PlainLocalPath(data)&&CanCreateUserData(data))return data;
  }
  PWSTR local=nullptr;
  if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData,0,nullptr,&local))) return {};
  auto path=std::filesystem::path(local)/L"YANVY";
  CoTaskMemFree(local);return PlainLocalPath(path)?path:std::filesystem::path{};
}
inline std::filesystem::path PreferenceRoot(const std::filesystem::path& install) {
  auto user=UserDataRoot();
  std::error_code error;
  return !user.empty()&&std::filesystem::is_regular_file(user/L"settings.ini",error)?user:install;
}
inline uint32_t ReadSchemaPreference(const std::filesystem::path& root) {
  UINT value=GetPrivateProfileIntW(L"input",L"schema",1,(root/L"settings.ini").c_str());
  return value<ime::kSchemaCount?value:1;
}
inline uint32_t ReadPinyinSchemaPreference(const std::filesystem::path& root) {
  UINT value=GetPrivateProfileIntW(L"input",L"pinyin_schema",1,(root/L"settings.ini").c_str());
  return value>0&&value<ime::kSchemaCount?value:1;
}
inline uint32_t ReadDoubleSchemaPreference(const std::filesystem::path& root) {
  UINT value=GetPrivateProfileIntW(L"input",L"double_schema",2,(root/L"settings.ini").c_str());
  return value>=2&&value<ime::kSchemaCount?value:2;
}
inline std::wstring ReadInputRevision(const std::filesystem::path& root) {
  wchar_t value[64]{};GetPrivateProfileStringW(L"input",L"change_id",L"",value,64,(root/L"settings.ini").c_str());return value;
}
inline std::wstring UserSid() {
  HANDLE token = nullptr;
  if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return {};
  DWORD length = 0;
  GetTokenInformation(token, TokenUser, nullptr, 0, &length);
  std::vector<BYTE> data(length);
  bool ok = GetTokenInformation(token, TokenUser, data.data(), length, &length);
  CloseHandle(token);
  if (!ok) return {};
  LPWSTR sid = nullptr;
  if (!ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(data.data())->User.Sid, &sid)) return {};
  std::wstring result(sid);
  LocalFree(sid);
  return result;
}
inline std::wstring ObjectName(const wchar_t* prefix) {
  DWORD session = 0;
  ProcessIdToSessionId(GetCurrentProcessId(), &session);
  return std::wstring(prefix) + L"YANVY-v5-" + UserSid() + L"-" + std::to_wstring(session);
}
inline std::wstring PipeName() { return ObjectName(L"\\\\.\\pipe\\"); }
// Bound both the connection and each IO operation. A failed engine never
// leaves a host application waiting indefinitely for a keyboard response.
inline bool Transfer(HANDLE pipe, void* buffer, DWORD size, bool write, DWORD timeout = 500) {
  OVERLAPPED ov{};
  ov.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  if (!ov.hEvent) return false;
  DWORD done = 0;
  BOOL ok = write ? WriteFile(pipe, buffer, size, &done, &ov) : ReadFile(pipe, buffer, size, &done, &ov);
  if (!ok && GetLastError() == ERROR_IO_PENDING) {
    if (WaitForSingleObject(ov.hEvent, timeout) == WAIT_OBJECT_0)
      ok = GetOverlappedResult(pipe, &ov, &done, FALSE);
    else {
      CancelIoEx(pipe, &ov);
      GetOverlappedResult(pipe, &ov, &done, TRUE);
      ok = FALSE;
    }
  }
  CloseHandle(ov.hEvent);
  return ok && done == size;
}
inline bool Call(const Request& request, Response& response) {
  HANDLE pipe = INVALID_HANDLE_VALUE;
  for (int attempt=0; attempt<20; ++attempt) {
    pipe = CreateFileW(PipeName().c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                      OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
    if (pipe != INVALID_HANDLE_VALUE) break;
    if (GetLastError() != ERROR_PIPE_BUSY && GetLastError() != ERROR_FILE_NOT_FOUND) return false;
    Sleep(5);
  }
  if (pipe == INVALID_HANDLE_VALUE) return false;
  DWORD mode = PIPE_READMODE_MESSAGE;
  bool ok = SetNamedPipeHandleState(pipe, &mode, nullptr, nullptr) &&
      Transfer(pipe, const_cast<Request*>(&request), sizeof(request), true) &&
      Transfer(pipe, &response, sizeof(response), false);
  BYTE ack = 1;
  if (ok) Transfer(pipe, &ack, sizeof(ack), true);
  CloseHandle(pipe);
  return ok && response.magic == kMagic && response.version == kProtocol && response.ok;
}
inline void StartBroker(const std::filesystem::path& root) {
  auto exe = root / L"YANVY-Server.exe";
  std::wstring command = L"\"" + exe.wstring() + L"\" --serve";
  STARTUPINFOW si{sizeof(si)};
  PROCESS_INFORMATION pi{};
  if (CreateProcessW(exe.c_str(), command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
                     nullptr, root.c_str(), &si, &pi)) {
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
  }
}
inline void OpenSettings(const std::filesystem::path& root) {
  auto executable=root/L"YANVY-Settings.exe";std::wstring command=L"\""+executable.wstring()+L"\"";
  STARTUPINFOW start{sizeof(start)};PROCESS_INFORMATION process{};
  if(CreateProcessW(executable.c_str(),command.data(),nullptr,nullptr,FALSE,0,nullptr,root.c_str(),&start,&process)){
    CloseHandle(process.hThread);CloseHandle(process.hProcess);
  }
}
}
