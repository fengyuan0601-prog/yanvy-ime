// SPDX-License-Identifier: BSD-3-Clause
// Native UAC bootstrap: configure all installer-owned temporary files on E:
// before the NSIS runtime starts. It executes only the embedded installer or
// the installation's explicitly named uninstaller, without a command shell.
#include "common.hpp"
#include <shellapi.h>
#include <objbase.h>

namespace {
int Fail(const wchar_t* text) { MessageBoxW(nullptr,text,L"言舟 · YANVY",MB_OK|MB_ICONERROR); return 1; }
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,PWSTR arguments,int) {
  const std::filesystem::path temporary = L"E:\\Codex\\Temp";
  std::error_code error;
  if (!std::filesystem::is_directory(L"E:\\",error)) return Fail(L"此开发测试包要求可用的 E 盘，以按约定存放临时文件和程序。");
  std::filesystem::create_directories(temporary,error);
  if (error || !SetEnvironmentVariableW(L"TEMP",temporary.c_str()) || !SetEnvironmentVariableW(L"TMP",temporary.c_str()) ||
      !SetEnvironmentVariableW(L"YANVY_SETUP_TEMP_READY",L"1")) return Fail(L"无法准备 E:\\Codex\\Temp。尚未开始系统注册。");
  const HRSRC resource = FindResourceW(instance,MAKEINTRESOURCEW(101),RT_RCDATA);
  bool installer = resource != nullptr;
  std::filesystem::path payload;
  if (installer) {
    HGLOBAL loaded = LoadResource(instance,resource);
    const void* bytes = loaded ? LockResource(loaded) : nullptr;
    const DWORD size = SizeofResource(instance,resource);
    if (!bytes || !size) return Fail(L"安装载荷损坏。尚未开始系统注册。");
    GUID unique{}; if (FAILED(CoCreateGuid(&unique))) return Fail(L"无法准备独立的安装临时文件。");
    wchar_t name[64]{}; StringFromGUID2(unique,name,64);
    payload = temporary/(std::wstring(L"YANVY-payload-")+name+L".exe");
    HANDLE output = CreateFileW(payload.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_TEMPORARY,nullptr);
    if (output == INVALID_HANDLE_VALUE) return Fail(L"无法在 E 盘创建安装临时文件。");
    DWORD written=0;
    bool ok=WriteFile(output,bytes,size,&written,nullptr) && written==size;
    CloseHandle(output);
    if (!ok) { DeleteFileW(payload.c_str()); return Fail(L"安装载荷写入失败，尚未开始系统注册。"); }
  } else {
    payload=yanvy::ModuleDir()/L"Uninstall-core.exe";
    if (!std::filesystem::is_regular_file(payload,error)) return Fail(L"未找到言舟卸载组件。未改动系统输入法注册。");
  }
  std::wstring command=L"\""+payload.wstring()+L"\" "+(arguments?arguments:L"");
  STARTUPINFOW si{sizeof(si)};PROCESS_INFORMATION process{};
  if (!CreateProcessW(payload.c_str(),command.data(),nullptr,nullptr,FALSE,0,nullptr,temporary.c_str(),&si,&process)) {
    if(installer)DeleteFileW(payload.c_str());
    return Fail(L"无法启动安装或卸载组件。请保留错误场景并反馈。");
  }
  CloseHandle(process.hThread);
  DWORD code=0;
  if(installer) {
    WaitForSingleObject(process.hProcess,INFINITE);
    GetExitCodeProcess(process.hProcess,&code);
  }
  CloseHandle(process.hProcess);
  // Let the NSIS uninstaller remove this bootstrap after it has exited.
  if(installer)DeleteFileW(payload.c_str());
  return int(code);
}
