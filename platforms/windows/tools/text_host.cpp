// SPDX-License-Identifier: BSD-3-Clause
// Manual, synthetic-input acceptance host. This is an ordinary native RichEdit
// control: no injected input, hooks, TSF mocks, profile changes or other windows.
// Only a fixed successful test string can be persisted, on an explicit button
// click, to a new explicitly supplied file. Unexpected input is never saved.
#include <windows.h>
#include <shellapi.h>
#include <richedit.h>
#include <ole2.h>
#include <msctf.h>
#include <wrl/client.h>
#include "ids.hpp"
#include "msctf_compat.hpp"
#include "text_host_fixture.hpp"
#include <cstring>
#include <filesystem>
#include <string>

namespace {
using Microsoft::WRL::ComPtr;
constexpr int save_id=101;
constexpr wchar_t expected[]=L"你好abc中国";
HWND editor=nullptr,status=nullptr,save=nullptr,instructions=nullptr,diagnostics=nullptr;
HFONT font=nullptr;
std::filesystem::path output;
template<class Function>Function ResolveExport(HMODULE module,const char* name) {
  auto symbol=GetProcAddress(module,name);Function function=nullptr;
  static_assert(sizeof(function)==sizeof(symbol));std::memcpy(&function,&symbol,sizeof(function));return function;
}
class LocalTip {
 public:
  std::wstring Describe() const {
    if(!module_||!manager_||!profiles_)return L"系统安装模式；未配置本地测试 DLL。";
    using Snapshot=HRESULT(WINAPI*)(yanvy::TextHostDiagnostics*,DWORD);
    auto snapshot=ResolveExport<Snapshot>(module_,"YanvyGetTextHostDiagnostics");
    yanvy::TextHostDiagnostics values;
    if(!snapshot||FAILED(snapshot(&values,sizeof(values))))return L"此测试 DLL 不提供只读状态。";
    ComPtr<ITfCompartmentMgr> compartments;ComPtr<ITfCompartment> mode;
    VARIANT current;VariantInit(&current);
    const wchar_t* input=L"未知";
    if(SUCCEEDED(manager_.As(&compartments))&&SUCCEEDED(compartments->GetCompartment(GUID_COMPARTMENT_KEYBOARD_OPENCLOSE,&mode))&&
        SUCCEEDED(mode->GetValue(&current))&&current.vt==VT_I4)input=current.lVal?L"中文":L"英文";
    VariantClear(&current);
    TF_INPUTPROCESSORPROFILE active{};
    const bool selected=profiles_->GetActiveProfile(GUID_TFCAT_TIP_KEYBOARD,&active)==S_OK&&active.clsid==clsid_&&active.guidProfile==profile_;
    wchar_t line[384]{};
    swprintf(line,std::size(line),L"只读状态：%ls；profile=%ls；TIP 激活/释放=%ld/%ld\nTest=%ld Down=%ld Preserved=%ld Cancel=%ld 线程失焦=%ld\n文档焦点=%ld 选择取消=%ld 引擎Focus=%ld（不含键值或文本）",
        input,selected?L"本地测试":L"其他/无键盘分类",values.activations,values.deactivations,values.test_down,values.key_down,values.preserved,
        values.cancellations,values.thread_blur,values.document_focus,values.selection_cancel,values.broker_focus);
    return line;
  }
  HRESULT Start(const std::filesystem::path& dll,const std::wstring& suffix,const std::filesystem::path& user_root) {
    if(!dll.is_absolute()||!user_root.is_absolute())return E_INVALIDARG;
    module_=LoadLibraryExW(dll.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    if(!module_)return HRESULT_FROM_WIN32(GetLastError());
    using Configure=HRESULT(WINAPI*)(const wchar_t*,const wchar_t*);
    auto configure=ResolveExport<Configure>(module_,"YanvyConfigureTextHost");
    // Refuse a distributable DLL: it cannot isolate the broker or preferences.
    if(!configure)return E_NOINTERFACE;
    HRESULT hr=configure(suffix.c_str(),user_root.c_str());if(FAILED(hr))return hr;
    hr=CoCreateInstance(CLSID_TF_ThreadMgr,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&manager_));if(FAILED(hr))return hr;
    hr=manager_->Activate(&client_);if(FAILED(hr))return hr;
    active_=true;
    hr=CoCreateInstance(CLSID_TF_InputProcessorProfiles,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&profiles_));if(FAILED(hr))return hr;
    restore_=profiles_->GetActiveProfile(GUID_TFCAT_TIP_KEYBOARD,&previous_)==S_OK;
    using GetFactory=HRESULT(WINAPI*)(REFCLSID,REFIID,void**);
    auto get_factory=ResolveExport<GetFactory>(module_,"DllGetClassObject");
    if(!get_factory)return E_NOINTERFACE;
    hr=get_factory(yanvy::kService,IID_PPV_ARGS(&factory_));if(FAILED(hr))return hr;
    hr=CoCreateGuid(&clsid_);if(FAILED(hr))return hr;
    hr=CoCreateGuid(&profile_);if(FAILED(hr))return hr;
    hr=CoRegisterClassObject(clsid_,factory_.Get(),CLSCTX_INPROC_SERVER,REGCLS_MULTIPLEUSE,&registration_);if(FAILED(hr))return hr;
    language_=LOWORD(GetKeyboardLayout(0));
    const wchar_t name[]=L"YANVY isolated RichEdit acceptance";
    // Values from WinSDK msctf.h: LOCALPROCESS=4, HIDDENINSETTINGUI=2.
    hr=profiles_->RegisterProfile(clsid_,language_,profile_,name,UINT(std::size(name)-1),L"",0,0,nullptr,0,TRUE,6);
    if(FAILED(hr))return hr;
    registered_=true;
    // Zero flags confines activation to this calling thread and never updates
    // the user's registry or the desktop/session input method selection.
    hr=profiles_->ActivateProfile(TF_PROFILETYPE_INPUTPROCESSOR,language_,clsid_,profile_,nullptr,0);
    return hr==S_OK?S_OK:FAILED(hr)?hr:E_FAIL;
  }
  ~LocalTip() {
    if(registered_){
      profiles_->DeactivateProfile(TF_PROFILETYPE_INPUTPROCESSOR,language_,clsid_,profile_,nullptr,0);
      profiles_->UnregisterProfile(clsid_,language_,profile_,4);
    }
    if(restore_)profiles_->ActivateProfile(previous_.dwProfileType,previous_.langid,previous_.clsid,previous_.guidProfile,previous_.hkl,0);
    if(registration_)CoRevokeClassObject(registration_);
    factory_.Reset();profiles_.Reset();
    if(active_)manager_->Deactivate();
    manager_.Reset();
    // Keep the module mapped until process exit: TSF/RichEdit may retain a COM
    // reference after unregistration. Never unload code out from under it.
  }
 private:
  HMODULE module_=nullptr;bool active_=false,registered_=false,restore_=false;
  DWORD registration_=0;GUID clsid_{},profile_{};LANGID language_=0;TfClientId client_=TF_CLIENTID_NULL;
  TF_INPUTPROCESSORPROFILE previous_{};
  ComPtr<ITfThreadMgr> manager_;ComPtr<ITfInputProcessorProfileMgr> profiles_;ComPtr<IClassFactory> factory_;
};
LocalTip* local_state=nullptr;
void Layout(HWND window) {
  RECT r{};GetClientRect(window,&r);
  const int width=(r.right>40?r.right-40:0);
  MoveWindow(instructions,20,16,width,74,TRUE);
  MoveWindow(editor,20,96,width,(r.bottom>265?r.bottom-265:0),TRUE);
  MoveWindow(save,20,r.bottom-155,250,36,TRUE);
  MoveWindow(diagnostics,20,r.bottom-111,width,75,TRUE);
  MoveWindow(status,20,r.bottom-32,width,30,TRUE);
}
void SaveResult() {
  const int length=GetWindowTextLengthW(editor);
  // The length bound also ensures accidentally pasted private text is not read.
  if(length!=int(std::size(expected)-1)) {
    SetWindowTextW(status,L"未通过：请只输入上方固定的合成测试文本。没有保存输入内容。");return;
  }
  wchar_t value[std::size(expected)]{};
  GetWindowTextW(editor,value,int(std::size(value)));
  if(std::wstring(value)!=expected) {
    SecureZeroMemory(value,sizeof(value));
    SetWindowTextW(status,L"未通过：文本与合成样例不同。没有保存输入内容。");return;
  }
  SecureZeroMemory(value,sizeof(value));
  HANDLE file=CreateFileW(output.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
  if(file==INVALID_HANDLE_VALUE) {
    SetWindowTextW(status,L"无法保存：输出文件必须尚不存在，且目录可写。");return;
  }
#ifdef _WIN64
  constexpr char report[]="{\"passed\":true,\"host\":\"native-rich-edit\",\"architecture\":\"x64\",\"text\":\"\u4f60\u597dabc\u4e2d\u56fd\"}\n";
#else
  constexpr char report[]="{\"passed\":true,\"host\":\"native-rich-edit\",\"architecture\":\"x86\",\"text\":\"\u4f60\u597dabc\u4e2d\u56fd\"}\n";
#endif
  DWORD written=0;
  const bool ok=WriteFile(file,report,DWORD(sizeof(report)-1),&written,nullptr)&&written==sizeof(report)-1;
  CloseHandle(file);
  SetWindowTextW(status,ok?L"通过：已保存固定合成测试结果。":L"保存失败；请使用新的输出文件重试。");
}
LRESULT CALLBACK WindowProc(HWND window,UINT message,WPARAM wp,LPARAM lp) {
  switch(message) {
    case WM_CREATE: {
      auto instance=reinterpret_cast<CREATESTRUCTW*>(lp)->hInstance;
      instructions=CreateWindowW(L"STATIC",L"合成测试专用，请勿输入个人内容。\n用实际键盘输入：你好 → Ctrl+空格 → abc → Ctrl+空格 → 中国\n最终文本：你好abc中国；点击下方按钮仅保存此固定样例。",
          WS_CHILD|WS_VISIBLE,0,0,0,0,window,nullptr,instance,nullptr);
      editor=CreateWindowExW(WS_EX_CLIENTEDGE,MSFTEDIT_CLASS,L"",WS_CHILD|WS_VISIBLE|WS_TABSTOP|WS_VSCROLL|ES_MULTILINE|ES_AUTOVSCROLL|ES_WANTRETURN,
          0,0,0,0,window,nullptr,instance,nullptr);
      save=CreateWindowW(L"BUTTON",L"验证并保存合成测试结果",WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_PUSHBUTTON,
          0,0,0,0,window,reinterpret_cast<HMENU>(save_id),instance,nullptr);
      status=CreateWindowW(L"STATIC",L"尚未保存。此工具不能证明按键来源或全部应用兼容。",WS_CHILD|WS_VISIBLE,0,0,0,0,window,nullptr,instance,nullptr);
      diagnostics=CreateWindowW(L"STATIC",L"",WS_CHILD|WS_VISIBLE,0,0,0,0,window,nullptr,instance,nullptr);
      if(!instructions||!editor||!save||!status||!diagnostics)return -1;
      font=CreateFontW(-20,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,DEFAULT_QUALITY,DEFAULT_PITCH,L"Segoe UI");
      for(auto child:{instructions,editor,save,status,diagnostics})SendMessageW(child,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);
      SendMessageW(editor,EM_SETTEXTMODE,TM_PLAINTEXT,0);
      // Enable the standard RichEdit TSF integration, without an IME API shim.
      SendMessageW(editor,EM_SETEDITSTYLE,SES_USECTF,SES_USECTF);
      SendMessageW(editor,EM_EXLIMITTEXT,0,256);
      SetTimer(window,1,500,nullptr);
      return 0;
    }
    case WM_SIZE: Layout(window);return 0;
    case WM_SETFOCUS: if(editor)SetFocus(editor);return 0;
    case WM_TIMER: if(local_state)SetWindowTextW(diagnostics,local_state->Describe().c_str());return 0;
    case WM_COMMAND: if(LOWORD(wp)==save_id&&HIWORD(wp)==BN_CLICKED){SaveResult();return 0;}break;
    case WM_DESTROY: PostQuitMessage(0);return 0;
  }
  return DefWindowProcW(window,message,wp,lp);
}
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,PWSTR,int show) {
  int count=0;auto args=CommandLineToArgvW(GetCommandLineW(),&count);
  if(!args)return 1;
  std::filesystem::path local_tip,user_root;std::wstring name_space;bool valid=true;
  for(int i=1;i<count;i+=2){
    if(i+1==count){valid=false;break;}
    const std::wstring flag=args[i];
    if(flag==L"--output"&&output.empty())output=args[i+1];
    else if(flag==L"--local-tip"&&local_tip.empty())local_tip=args[i+1];
    else if(flag==L"--namespace"&&name_space.empty())name_space=args[i+1];
    else if(flag==L"--user-root"&&user_root.empty())user_root=args[i+1];
    else {valid=false;break;}
  }
  LocalFree(args);
  if(!valid||!output.is_absolute()||output.lexically_normal()!=output||output.filename().empty())return 2;
  if(local_tip.empty()!=user_root.empty()||local_tip.empty()!=name_space.empty())return 2;
  if(FAILED(OleInitialize(nullptr)))return 3;
  int result=5;
  {
  LocalTip tip;
  local_state=&tip;
  if(!local_tip.empty()){
    const HRESULT hr=tip.Start(local_tip,name_space,user_root);
    if(FAILED(hr)){
      wchar_t error[160]{};swprintf(error,std::size(error),L"进程本地输入法初始化失败：0x%08lX。未进行系统安装。",static_cast<unsigned long>(hr));
      MessageBoxW(nullptr,error,L"YANVY 合成验收工具",MB_OK|MB_ICONERROR);
      result=6;
    }else result=0;
  }else result=0;
  if(!result){
  HMODULE rich=LoadLibraryW(L"Msftedit.dll");
  if(!rich)result=4;
  else{
  WNDCLASSW klass{};klass.lpfnWndProc=WindowProc;klass.hInstance=instance;
  klass.lpszClassName=L"YANVY-Synthetic-Text-Host";klass.hCursor=LoadCursorW(nullptr,IDC_IBEAM);
  klass.hbrBackground=reinterpret_cast<HBRUSH>(COLOR_WINDOW+1);
  RegisterClassW(&klass);
#ifdef _WIN64
  const wchar_t title[]=L"YANVY 原生输入验收 · x64 · 合成文本专用";
#else
  const wchar_t title[]=L"YANVY 原生输入验收 · x86 · 合成文本专用";
#endif
  HWND window=CreateWindowW(klass.lpszClassName,title,WS_OVERLAPPEDWINDOW,CW_USEDEFAULT,CW_USEDEFAULT,860,560,nullptr,nullptr,instance,nullptr);
  result=5;
  if(window) {
    ShowWindow(window,show);UpdateWindow(window);SetFocus(editor);
    MSG message{};
    while((result=int(GetMessageW(&message,nullptr,0,0)))>0){TranslateMessage(&message);DispatchMessageW(&message);}
  }
  if(font)DeleteObject(font);
  FreeLibrary(rich);
  }
  local_state=nullptr;
  }
  }
  OleUninitialize();return result;
}
