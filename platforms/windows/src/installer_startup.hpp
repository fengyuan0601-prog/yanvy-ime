// SPDX-License-Identifier: BSD-3-Clause
#pragma once
#include <windows.h>
#include <objbase.h>
#include <oleauto.h>
#include <exdisp.h>
#include <shlobj.h>
#include <shldisp.h>
#include <shobjidl.h>
#include <servprov.h>
#include <wrl/client.h>
#include <filesystem>
#include <vector>
#ifdef ShellExecute
#undef ShellExecute
#endif

namespace yanvy {
struct InstallerToken {
  bool elevated=false,app_container=false;
  DWORD integrity=0,session=0;
};
inline HRESULT ReadInstallerToken(HANDLE process,InstallerToken& result){
  HANDLE token=nullptr;if(!OpenProcessToken(process,TOKEN_QUERY,&token))return HRESULT_FROM_WIN32(GetLastError());
  struct Close{HANDLE value;~Close(){CloseHandle(value);}} close{token};
  TOKEN_ELEVATION elevation{};DWORD container=0,session=0,size=0;
  if(!GetTokenInformation(token,TokenElevation,&elevation,sizeof(elevation),&size)||
      !GetTokenInformation(token,TokenIsAppContainer,&container,sizeof(container),&size)||
      !GetTokenInformation(token,TokenSessionId,&session,sizeof(session),&size))return HRESULT_FROM_WIN32(GetLastError());
  GetTokenInformation(token,TokenIntegrityLevel,nullptr,0,&size);
  if(!size)return E_FAIL;
  std::vector<BYTE> bytes(size);
  if(!GetTokenInformation(token,TokenIntegrityLevel,bytes.data(),size,&size))return HRESULT_FROM_WIN32(GetLastError());
  const auto sid=reinterpret_cast<TOKEN_MANDATORY_LABEL*>(bytes.data())->Label.Sid;
  if(!IsValidSid(sid)||!*GetSidSubAuthorityCount(sid))return E_FAIL;
  result={elevation.TokenIsElevated!=0,container!=0,*GetSidSubAuthority(sid,*GetSidSubAuthorityCount(sid)-1),session};
  return S_OK;
}
inline bool OrdinaryInstallerToken(const InstallerToken& value){
  return !value.elevated&&!value.app_container&&value.integrity>=SECURITY_MANDATORY_MEDIUM_RID&&value.integrity<SECURITY_MANDATORY_HIGH_RID;
}
inline DWORD InstallerUserPermission(){
  InstallerToken token;const HRESULT read=ReadInstallerToken(GetCurrentProcess(),token);
  return FAILED(read)?DWORD(read):OrdinaryInstallerToken(token)?ERROR_SUCCESS:ERROR_ELEVATION_REQUIRED;
}
// Ask the original interactive desktop to open THIS executable, with one fixed
// UI-only argument. Never reuse the elevated account's linked token: an OTS UAC
// consent may belong to a different administrator. No worker or selected path
// is forwarded. The child checks its token again; the marker prevents loops.
inline HRESULT OpenInstallerOnDesktop(const std::filesystem::path& executable){
  DWORD pid=0;const auto shell_window=GetShellWindow();
  if(!shell_window||!GetWindowThreadProcessId(shell_window,&pid)||!pid)return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
  HANDLE process=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid);
  if(!process)return HRESULT_FROM_WIN32(GetLastError());
  InstallerToken shell;HRESULT read=ReadInstallerToken(process,shell);CloseHandle(process);
  if(FAILED(read))return read;
  DWORD session=0;if(!ProcessIdToSessionId(GetCurrentProcessId(),&session))return HRESULT_FROM_WIN32(GetLastError());
  if(!OrdinaryInstallerToken(shell)||shell.session!=session)return HRESULT_FROM_WIN32(ERROR_ELEVATION_REQUIRED);
  using Microsoft::WRL::ComPtr;
  ComPtr<IShellWindows> windows;
  HRESULT hr=CoCreateInstance(CLSID_ShellWindows,nullptr,CLSCTX_LOCAL_SERVER,IID_PPV_ARGS(&windows));if(FAILED(hr))return hr;
  VARIANT location{},root{};location.vt=VT_I4;location.lVal=CSIDL_DESKTOP;
  long hwnd=0;ComPtr<IDispatch> desktop;
  hr=windows->FindWindowSW(&location,&root,SWC_DESKTOP,&hwnd,SWFO_NEEDDISPATCH,&desktop);if(FAILED(hr)||!desktop)return FAILED(hr)?hr:E_FAIL;
  ComPtr<IServiceProvider> provider;hr=desktop.As(&provider);if(FAILED(hr))return hr;
  ComPtr<IShellBrowser> browser;hr=provider->QueryService(SID_STopLevelBrowser,IID_PPV_ARGS(&browser));if(FAILED(hr))return hr;
  ComPtr<IShellView> view;hr=browser->QueryActiveShellView(&view);if(FAILED(hr))return hr;
  ComPtr<IDispatch> background;hr=view->GetItemObject(SVGIO_BACKGROUND,IID_PPV_ARGS(&background));if(FAILED(hr))return hr;
  ComPtr<IShellFolderViewDual> folder;hr=background.As(&folder);if(FAILED(hr))return hr;
  ComPtr<IDispatch> application;hr=folder->get_Application(&application);if(FAILED(hr))return hr;
  ComPtr<IShellDispatch2> dispatch;hr=application.As(&dispatch);if(FAILED(hr))return hr;
  BSTR file=SysAllocString(executable.c_str()),arguments=SysAllocString(L"--desktop-ui"),directory=SysAllocString(executable.parent_path().c_str()),verb=SysAllocString(L"open");
  if(!file||!arguments||!directory||!verb){SysFreeString(file);SysFreeString(arguments);SysFreeString(directory);SysFreeString(verb);return E_OUTOFMEMORY;}
  VARIANT args{},cwd{},operation{},show{};args.vt=cwd.vt=operation.vt=VT_BSTR;args.bstrVal=arguments;cwd.bstrVal=directory;operation.bstrVal=verb;show.vt=VT_I4;show.lVal=SW_SHOWNORMAL;
  hr=dispatch->ShellExecute(file,args,cwd,operation,show);
  SysFreeString(file);SysFreeString(arguments);SysFreeString(directory);SysFreeString(verb);return hr;
}
}
