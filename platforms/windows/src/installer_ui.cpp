// SPDX-License-Identifier: BSD-3-Clause
// The public setup UI stays unelevated. Only an explicit install/uninstall
// action starts the elevated, embedded offline transaction.
#include "installer_ui.hpp"
#include "appearance.hpp"
#include "ui_controls.hpp"
#include "machine_store.hpp"
#include "installer_diagnostics.hpp"
#include "user_cleanup.hpp"
#include "broker_upgrade.hpp"
#include "package_integrity.hpp"
#include "installer_transaction.hpp"
#include "installer_startup.hpp"
#include <commctrl.h>
#include <dwmapi.h>
#include <shobjidl.h>
#include <shlobj.h>
#include <shellapi.h>
#include <wrl/client.h>

namespace yanvy {
bool ValidInstallPath(const std::filesystem::path& path){
  const auto text=path.wstring();
  if(text.size()<6||text.size()>200||text.find_first_of(L"\"\r\n\t/*?<>|")!=std::wstring::npos)return false;
  if(!((text[0]>=L'A'&&text[0]<=L'Z')||(text[0]>=L'a'&&text[0]<=L'z'))||text[1]!=L':'||text[2]!=L'\\')return false;
  auto drive=path.root_path().wstring();UINT drive_type=GetDriveTypeW(drive.c_str());
  if(drive_type==DRIVE_REMOTE||drive_type==DRIVE_CDROM)return false;
  if(text.find(L':',2)!=std::wstring::npos||text.back()==L' '||text.back()==L'.')return false;
  if(path.lexically_normal()!=path||!path.has_filename())return false;
  for(auto part:path.relative_path())if(part==L".."||part==L"."||part.wstring().back()==L' '||part.wstring().back()==L'.')return false;
  for(auto parent=path;!parent.empty();parent=parent.parent_path()){
    auto attributes=GetFileAttributesW(parent.c_str());
    if(attributes!=INVALID_FILE_ATTRIBUTES&&(attributes&FILE_ATTRIBUTE_REPARSE_POINT))return false;
    if(parent==parent.root_path())break;
  }
  return true;
}
namespace {
DWORD ReadPreviousInstallation(std::filesystem::path& runtime,std::filesystem::path& data){
  runtime.clear();data.clear();wchar_t previous[32768]{};DWORD bytes=sizeof(previous);
  const LSTATUS read=RegGetValueW(HKEY_LOCAL_MACHINE,L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\YANVY-dev",L"InstallLocation",RRF_RT_REG_SZ|RRF_SUBKEY_WOW6464KEY,nullptr,previous,&bytes);
  if(read==ERROR_FILE_NOT_FOUND||read==ERROR_PATH_NOT_FOUND)return ERROR_SUCCESS;
  if(read!=ERROR_SUCCESS)return DWORD(read);
  runtime=std::filesystem::path(previous);data=SUCCEEDED(ValidateMachineRuntime(runtime))?InstalledDataRoot(runtime):runtime;
  return ERROR_SUCCESS;
}
std::filesystem::path DefaultInstall(){
  auto preferred=std::filesystem::path(L"E:\\Codex\\Programs");
  if(GetDriveTypeW(L"E:\\")==DRIVE_FIXED&&std::filesystem::is_directory(preferred))return SelectDefaultInstall(true,{});
  PWSTR local=nullptr;std::filesystem::path result;
  if(SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData,0,nullptr,&local))){result=SelectDefaultInstall(false,std::filesystem::path(local));CoTaskMemFree(local);}
  return result;
}
constexpr int primary_id=101,secondary_id=102,browse_id=103,notes_id=104,settings_id=105;
struct View {
  InstallerTransaction transaction;
  HINSTANCE instance{};HWND window{},primary{},secondary{},browse{},notes{},settings{},progress{},heading{},status_control{},path_control{};
  HWND schema_box{};uint32_t schema=1;
  UINT dpi=96;int page=0;bool uninstall=false,preview=false;DWORD result=0;HANDLE worker=nullptr;
  enum class Stage { none, prepare_data, machine, enable_user, stop_legacy, apply_schema, verify_broker, cleanup_data };
  Stage stage=Stage::none;bool data_prepared=false,configuration_failed=false,show_diagnostics=false;
  DWORD machine_result=0;
  bool startup_blocked=false;
  DWORD cleanup_result=0;bool cleanup_done=false;
  InstallerPhase diagnostic_phase=InstallerPhase::unknown;
  std::filesystem::path previous_install,previous_data,uninstall_data;
  UpgradeBrokerSnapshot upgrade_broker;
  bool broker_checked=false;
  HFONT body{},caption_font{},headline{},brand{},strong{};HICON icon{};ULONG_PTR graphics{};
  HBRUSH white=CreateSolidBrush(Canvas()),card=CreateSolidBrush(RGB(255,255,255));
  std::filesystem::path target=DefaultInstall();std::wstring status;
  View(){Gdiplus::GdiplusStartupInput input;input.SuppressExternalCodecs=TRUE;Gdiplus::GdiplusStartup(&graphics,&input,nullptr);}
  ~View(){ClearFonts();DeleteObject(white);DeleteObject(card);if(worker)CloseHandle(worker);if(graphics)Gdiplus::GdiplusShutdown(graphics);}
  int D(int n)const{return MulDiv(n,int(dpi),96);}
  bool NeedsDiagnostic()const{return page==3||configuration_failed||cleanup_result!=0;}
  RECT R(int l,int t,int r,int b)const{return {D(l),D(t),D(r),D(b)};}
  void ClearFonts(){for(auto font:{body,caption_font,headline,brand,strong})if(font)DeleteObject(font);}
#include "installer_surface.hpp"
  void Browse(){
    Microsoft::WRL::ComPtr<IFileOpenDialog> dialog;
    if(FAILED(CoCreateInstance(CLSID_FileOpenDialog,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&dialog))))return;
    dialog->SetOptions(FOS_PICKFOLDERS|FOS_FORCEFILESYSTEM|FOS_PATHMUSTEXIST|FOS_DONTADDTORECENT);dialog->SetTitle(L"选择词库目录（可复用本安装包的完整词库）");
    auto parent=target.parent_path().parent_path();Microsoft::WRL::ComPtr<IShellItem> start;if(SUCCEEDED(SHCreateItemFromParsingName(parent.c_str(),nullptr,IID_PPV_ARGS(&start))))dialog->SetFolder(start.Get());
    if(SUCCEEDED(dialog->Show(window))){Microsoft::WRL::ComPtr<IShellItem> item;PWSTR value=nullptr;
      if(SUCCEEDED(dialog->GetResult(&item))&&SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH,&value))){auto selected=std::filesystem::path(value);CoTaskMemFree(value);
        std::error_code error;const bool empty=std::filesystem::is_empty(selected,error)&&!error;
        PackageManifest manifest;const bool reusable=!empty&&LoadPackageManifest(instance,manifest)&&InspectPackageDestination(selected,manifest,true)==PackageDestination::reusable;
        selected=ResolveSelectedInstallPath(selected,empty,reusable);
        if(ValidInstallPath(selected)){target=selected;status=L"可直接修改上方完整路径；设置和缓存单独保存。";}else status=L"请选择本地普通文件夹，不使用网络路径或链接。";Refresh();}}
  }
  std::filesystem::path Runtime()const{return uninstall?ModuleDir():MachineRuntimeRoot(kVersion);}
  std::filesystem::path Self()const{wchar_t path[32768]{};GetModuleFileNameW(nullptr,path,32768);return path;}
  bool Spawn(const std::filesystem::path& executable,const std::wstring& arguments,Stage next){
    if(next!=Stage::cleanup_data)diagnostic_phase=next==Stage::prepare_data?InstallerPhase::data_prepare:next==Stage::stop_legacy?InstallerPhase::cleanup:InstallerPhase::user_configuration;
    std::wstring command=L"\""+executable.wstring()+L"\" "+arguments;
    STARTUPINFOW startup{sizeof(startup)};PROCESS_INFORMATION process{};
    if(!CreateProcessW(executable.c_str(),command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,executable.parent_path().c_str(),&startup,&process))return false;
    CloseHandle(process.hThread);worker=process.hProcess;stage=next;page=1;SetTimer(window,1,200,nullptr);Refresh();return true;
  }
  bool StartMachine(){
    diagnostic_phase=uninstall?InstallerPhase::uninstall:InstallerPhase::machine_install;
    auto exe=Self();std::wstring arguments=std::wstring(uninstall?L"--worker-uninstall ":L"--worker-install ")+L"\""+target.wstring()+L"\"";
    if(!uninstall)arguments+=L" "+std::to_wstring(schema);
    SHELLEXECUTEINFOW launch{sizeof(launch)};launch.fMask=SEE_MASK_NOCLOSEPROCESS|SEE_MASK_NOASYNC|SEE_MASK_FLAG_NO_UI;
    launch.hwnd=window;launch.lpVerb=L"runas";launch.lpFile=exe.c_str();launch.lpParameters=arguments.c_str();launch.nShow=SW_HIDE;
    if(!ShellExecuteExW(&launch))return false;
    if(!launch.hProcess){SetLastError(ERROR_GEN_FAILURE);return false;}
    worker=launch.hProcess;stage=Stage::machine;page=1;status=uninstall?L"正在卸载系统组件…":L"正在安装受保护的系统组件…";SetTimer(window,1,200,nullptr);Refresh();return true;
  }
  void Start(){
    if(preview){status=L"界面预览不执行安装，也不更改系统输入法。";Refresh();return;}
    if(const DWORD permission=InstallerUserPermission()){startup_blocked=true;diagnostic_phase=InstallerPhase::startup;Fail(permission);return;}
    // A retry can select a different path. Cleanup ownership never transfers
    // from a previous failed attempt to the newly selected directory.
    configuration_failed=false;data_prepared=false;result=0;machine_result=0;cleanup_result=0;cleanup_done=false;broker_checked=false;diagnostic_phase=InstallerPhase::unknown;
    if(!ValidInstallPath(target)){diagnostic_phase=InstallerPhase::destination;result=10;status=L"位置无效：请选择本地普通文件夹。";page=3;Refresh();return;}
    const auto sid=UserSid();if(sid.empty()){Fail(ERROR_ACCESS_DENIED);return;}
    const DWORD locked=transaction.Acquire(L"Global\\YANVY.Setup.User."+sid);if(locked){Fail(locked);return;}
    if(!uninstall){const DWORD read=ReadPreviousInstallation(previous_install,previous_data);if(read){Fail(read);return;}}
    if(uninstall)uninstall_data=InstalledDataRoot(Runtime());
    if(!uninstall){const HRESULT captured=CaptureUpgradeBroker(previous_install,Runtime(),upgrade_broker);if(FAILED(captured)){Fail(DWORD(captured));return;}}
    status=uninstall?L"正在卸载系统组件…":L"正在准备所选位置的离线词库…";
    const bool started=uninstall?StartMachine():Spawn(Self(),L"--worker-prepare-data \""+target.wstring()+L"\"",Stage::prepare_data);
    if(!started)Fail(GetLastError());
  }
  std::wstring FailureText()const{
    if(result==ERROR_ELEVATION_REQUIRED){
      const auto environment=CollectInstallerEnvironment();
      if(environment.uac_setting==InstallerUac::disabled)return L"检测到 UAC 已关闭，无法取得普通用户环境。请恢复 Windows 用户账户控制后重试。";
      return L"未能取得普通用户环境。请查看诊断，并检查账户及“始终以管理员身份运行”设置。";
    }
    if(result==ERROR_ACCESS_DENIED||result==DWORD(E_ACCESSDENIED))return L"Windows 拒绝了访问，请检查当前账户与所选位置的权限。";
    if(result==ERROR_CANCELLED)return uninstall?L"已取消管理员授权，未执行卸载。":L"已取消管理员授权。词库准备内容将安全回退。";
    if(result==10)return L"目录已有文件，或不是有效的本地词库位置。";
    if(result==11)return L"已有安装未通过同包完整性确认，原文件已保留。请查看诊断。";
    if(result==kPackageMismatch)return L"已有文件不属于同一完整安装包，已保留。可更换词库目录；系统组件异常请查看诊断。";
    if(result==20||result==21)return L"文件处理失败，请检查所选目录的磁盘空间和权限。";
    if(result==23)return L"组件或系统依赖无法加载，请查看诊断。";
    if(result==24||result==25||result==26||result==29||result==32)return L"词库检查未通过，请查看诊断中的完整错误类别。";
    if(result==27)return L"无法访问词库目录，请检查位置和权限。";
    if(result==28)return L"路径包含链接或重解析点，请选择普通本地目录。";
    if(result==30)return uninstall?L"Windows 未完成输入法注销，程序文件已保留。":L"系统注册失败，请查看诊断和回退结果。";
    if(result==31)return L"安装失败，安全回退未完成。程序和词库已保留，请查看诊断。";
    if(result==ERROR_BUSY)return L"已有安装或卸载任务正在运行，请稍后再试。";
    wchar_t code[11]{};swprintf(code,11,L"0x%08lX",static_cast<unsigned long>(result));
    return std::wstring(L"操作未完成，错误码 ")+code+L"。请查看诊断。";
  }
  void Fail(DWORD code){
    result=code;status=FailureText();
    // An incomplete machine rollback can still reference this data directory.
    if(data_prepared&&code!=31&&!uninstall){
      if(Spawn(Self(),L"--worker-cleanup-data \""+target.wstring()+L"\"",Stage::cleanup_data))return;
      status+=L" 词库清理未完成，原文件已保留。";
    }
    page=3;stage=Stage::none;transaction.Release();Refresh();
  }
  bool UserStep(const wchar_t* executable,const std::wstring& args,Stage next){
    if(FAILED(ValidateMachineRuntime(Runtime()))){SetLastError(ERROR_ACCESS_DENIED);return false;}
    return Spawn(Runtime()/executable,args,next);
  }
  void Finish(){
    page=2;stage=Stage::none;
    const bool runtime_trusted=uninstall||SUCCEEDED(ValidateMachineRuntime(Runtime()));
    if(!runtime_trusted){configuration_failed=true;result=ERROR_ACCESS_DENIED;}
    if(!uninstall&&runtime_trusted&&!broker_checked){
      broker_checked=true;StartBroker(Runtime());status=L"正在确认新版引擎已启动…";
      if(UserStep(L"YANVY-Setup.exe",L"--verify-current-broker",Stage::verify_broker))return;
      configuration_failed=true;result=GetLastError();
    }
    if(!cleanup_done&&runtime_trusted&&MayCleanupPreviousInstallation(uninstall,configuration_failed,broker_checked)){
      cleanup_done=true;
      const auto clean=[&](const std::filesystem::path& path,bool data_only){
        if(path.empty()||SameMachinePath(path,Runtime())||(!uninstall&&SameMachinePath(path,target)))return;
        const DWORD code=CleanupUserInstallation(path,data_only);if(code&&!cleanup_result)cleanup_result=code;
      };
      if(uninstall)clean(uninstall_data,true);
      else{clean(previous_install,false);if(!SameMachinePath(previous_data,previous_install))clean(previous_data,true);}
      if(cleanup_result){
        if(uninstall){configuration_failed=true;result=cleanup_result;diagnostic_phase=InstallerPhase::cleanup;}
        else if(!configuration_failed)diagnostic_phase=InstallerPhase::cleanup;
      }
    }
    status=machine_result==3010?L"旧版被占用的文件将稍后清理；不会自动重启。":machine_result==33?L"新版已安装；部分旧版本尚未完全移除，已保留相关文件。可查看诊断。":L"任务已完成。";
    if(configuration_failed)status=uninstall?L"程序已卸载，部分用户文件或启动项未能清理。请查看诊断。":L"程序已安装，但当前用户配置未完成。请查看诊断；可打开设置检查方案。";
    else if(cleanup_result){status=L"新版已安装，部分旧文件正在使用或不可写，已保留。可查看诊断。";result=cleanup_result;}
    else result=machine_result;
    transaction.Release();Refresh();
  }
  void Complete(DWORD code){
    const auto finished=stage;stage=Stage::none;
    if(finished==Stage::cleanup_data){if(code==0)data_prepared=false;else status+=L" 词库清理未完成，文件已保留。";page=3;transaction.Release();Refresh();return;}
    if(finished==Stage::prepare_data){if(!PreparedDataAccepted(code)){Fail(code);return;}data_prepared=PreparedDataOwnedByAttempt(code);if(!StartMachine())Fail(GetLastError());return;}
    if(finished==Stage::machine){
      machine_result=code;
      if(code!=0&&code!=3010&&code!=33){Fail(code);return;}
      if(uninstall){
        HKEY run=nullptr;LSTATUS cleanup=RegOpenKeyExW(HKEY_CURRENT_USER,L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",0,KEY_SET_VALUE,&run);
        if(cleanup==ERROR_SUCCESS){cleanup=RegDeleteValueW(run,L"YANVY-dev");RegCloseKey(run);}
        if(cleanup!=ERROR_SUCCESS&&cleanup!=ERROR_FILE_NOT_FOUND&&cleanup!=ERROR_PATH_NOT_FOUND){configuration_failed=true;result=DWORD(cleanup);}
        Finish();return;
      }
      status=L"正在为当前用户启用言舟…";
      if(UserStep(L"YANVY-Setup.exe",L"--enable-user",Stage::enable_user))return;
      configuration_failed=true;result=GetLastError();Finish();return;
    }
    if(code!=0){configuration_failed=true;result=code;if(finished==Stage::stop_legacy)diagnostic_phase=InstallerPhase::cleanup;}
    if(finished==Stage::enable_user){
      status=L"正在切换到新版引擎…";std::wstring stop=L"--stop-previous-brokers";
      if(upgrade_broker.pid)stop+=L" "+std::to_wstring(upgrade_broker.pid)+L" "+std::to_wstring(upgrade_broker.created)+L" \""+previous_install.wstring()+L"\"";
      if(UserStep(L"YANVY-Setup.exe",stop,Stage::stop_legacy))return;
      configuration_failed=true;result=GetLastError();
    }
    if(finished==Stage::enable_user||finished==Stage::stop_legacy){status=L"正在保存所选输入方案…";if(UserStep(L"YANVY-Settings.exe",L"--apply-schema "+std::to_wstring(schema),Stage::apply_schema))return;configuration_failed=true;result=GetLastError();}
    Finish();
  }
};
LRESULT CALLBACK NotesProc(HWND window,UINT message,WPARAM wp,LPARAM lp){
  if(message==WM_CREATE){auto view=static_cast<View*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(view));
    auto data=FindResourceW(view->instance,MAKEINTRESOURCEW(102),RT_RCDATA);std::wstring text;
    if(view->show_diagnostics){
      auto phase=view->diagnostic_phase;
      if(view->result==26||(view->result&0xffff0000u)==0xa1260000u)phase=InstallerPhase::engine_check;
      auto raw=FormatInstallerDiagnostic(phase,view->result,CollectInstallerEnvironment());
      for(size_t index=0;index<raw.size();++index){if(raw[index]==L'\n'&&(index==0||raw[index-1]!=L'\r'))text+=L'\r';text+=raw[index];}
    }else if(data){auto bytes=static_cast<const char*>(LockResource(LoadResource(view->instance,data)));auto raw=Wide(std::string(bytes,SizeofResource(view->instance,data)));if(!raw.empty()&&raw.front()==0xfeff)raw.erase(0,1);
      for(size_t index=0;index<raw.size();++index){if(raw[index]==L'\n'&&(index==0||raw[index-1]!=L'\r'))text+=L'\r';text+=raw[index];}}
    auto edit=CreateWindowExW(WS_EX_CLIENTEDGE,L"EDIT",text.c_str(),WS_VISIBLE|WS_CHILD|WS_VSCROLL|ES_MULTILINE|ES_READONLY|ES_AUTOVSCROLL,0,0,1,1,window,reinterpret_cast<HMENU>(1),view->instance,nullptr);
    SendMessageW(edit,WM_SETFONT,reinterpret_cast<WPARAM>(view->body),TRUE);SendMessageW(edit,EM_SETMARGINS,EC_LEFTMARGIN|EC_RIGHTMARGIN,MAKELPARAM(view->D(14),view->D(14)));return 0;}
  if(message==WM_SIZE){MoveWindow(GetDlgItem(window,1),0,0,LOWORD(lp),HIWORD(lp),TRUE);return 0;}
  if(message==WM_CLOSE){DestroyWindow(window);return 0;}
  return DefWindowProcW(window,message,wp,lp);
}
LRESULT CALLBACK Proc(HWND window,UINT message,WPARAM wp,LPARAM lp){
  auto v=reinterpret_cast<View*>(GetWindowLongPtrW(window,GWLP_USERDATA));
  if(message==WM_NCCREATE){v=static_cast<View*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);v->window=window;SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(v));}
  if(!v)return DefWindowProcW(window,message,wp,lp);
  if(message==WM_CREATE){v->dpi=FitSurfaceDpi(window,GetDpiForWindow(window),800,560);v->Fonts();
    auto control=[&](const wchar_t* cls,const wchar_t* text,DWORD style,int id){return CreateWindowExW(0,cls,text,WS_CHILD|WS_VISIBLE|style,0,0,1,1,window,reinterpret_cast<HMENU>(INT_PTR(id)),v->instance,nullptr);};
    v->heading=control(L"STATIC",L"",SS_LEFT|SS_CENTERIMAGE,110);v->path_control=control(L"EDIT",L"",ES_AUTOHSCROLL|WS_TABSTOP|(v->uninstall?ES_READONLY:0),111);
    v->status_control=control(L"STATIC",L"",SS_LEFT,112);v->primary=control(L"BUTTON",L"",BS_OWNERDRAW|WS_TABSTOP,primary_id);
    v->schema_box=control(L"COMBOBOX",L"默认输入方案",CBS_DROPDOWNLIST|CBS_OWNERDRAWFIXED|CBS_HASSTRINGS|WS_TABSTOP,114);for(auto text:ime::kSchemaLabels)SendMessageW(v->schema_box,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(text));
    v->secondary=control(L"BUTTON",L"取消",BS_OWNERDRAW|WS_TABSTOP,secondary_id);v->browse=control(L"BUTTON",L"更改",BS_OWNERDRAW|WS_TABSTOP,browse_id);
    v->notes=control(L"BUTTON",L"版本与开源说明",BS_OWNERDRAW|WS_TABSTOP,notes_id);v->settings=control(L"BUTTON",L"打开设置",BS_OWNERDRAW|WS_TABSTOP,settings_id);
    v->progress=control(PROGRESS_CLASSW,L"安装进度",PBS_MARQUEE,113);v->status=v->preview?L"界面预览 · 不执行安装或更改系统注册":v->uninstall?L"卸载将请求 Windows 管理员授权。":L"安装包未签名。安装系统组件时，将请求管理员授权。";v->Layout();return 0;
  }
  if(message==WM_ERASEBKGND)return 1;
  if(message==WM_PAINT){PAINTSTRUCT ps{};HDC dc=BeginPaint(window,&ps);RECT rect{};GetClientRect(window,&rect);auto buffer=CreateCompatibleDC(dc);auto bitmap=CreateCompatibleBitmap(dc,rect.right,rect.bottom);auto old=SelectObject(buffer,bitmap);v->Draw(buffer,true);BitBlt(dc,0,0,rect.right,rect.bottom,buffer,0,0,SRCCOPY);SelectObject(buffer,old);DeleteObject(bitmap);DeleteDC(buffer);EndPaint(window,&ps);return 0;}
  if(message==WM_CTLCOLORSTATIC||message==WM_CTLCOLOREDIT||message==WM_CTLCOLORLISTBOX){auto dc=reinterpret_cast<HDC>(wp);SetBkMode(dc,TRANSPARENT);SetTextColor(dc,reinterpret_cast<HWND>(lp)==v->status_control?(v->page==3?RGB(165,58,44):RGB(94,116,133)):RGB(22,44,62));return reinterpret_cast<LRESULT>(reinterpret_cast<HWND>(lp)==v->path_control?v->card:v->white);}
  if(message==WM_MEASUREITEM){reinterpret_cast<MEASUREITEMSTRUCT*>(lp)->itemHeight=v->D(30);return TRUE;}
  if(message==WM_DRAWITEM){auto& item=*reinterpret_cast<DRAWITEMSTRUCT*>(lp);wchar_t text[80]{};
    if(item.CtlID==114){if(item.itemID==UINT(-1))return TRUE;auto p=Palette::Get(false);Fill(item.hDC,item.rcItem,(item.itemState&ODS_SELECTED)?p.selection:p.background);SendMessageW(item.hwndItem,CB_GETLBTEXT,item.itemID,reinterpret_cast<LPARAM>(text));auto r=item.rcItem;r.left+=v->D(10);Label(item.hDC,text,r,v->body,p.text);return TRUE;}
    GetWindowTextW(item.hwndItem,text,80);
    if(item.CtlID==notes_id){Fill(item.hDC,item.rcItem,Canvas());Label(item.hDC,text,item.rcItem,v->caption_font,RGB(12,109,108));if(item.itemState&ODS_FOCUS){RECT r=item.rcItem;InflateRect(&r,-v->D(3),-v->D(3));DrawFocusRect(item.hDC,&r);}}
    else {Fill(item.hDC,item.rcItem,item.CtlID==browse_id?RGB(255,255,255):Canvas());v->Button(item.hDC,item.rcItem,text,item.CtlID==primary_id,(item.itemState&ODS_DISABLED)!=0,(item.itemState&ODS_FOCUS)!=0,(item.itemState&ODS_SELECTED)!=0,SurfaceHot(item.hwndItem));}
    return TRUE;}
  if(message==WM_DPICHANGED){v->dpi=FitSurfaceDpi(window,HIWORD(wp),800,560);auto r=reinterpret_cast<RECT*>(lp);RECT size{0,0,v->D(800),v->D(560)};AdjustWindowRectExForDpi(&size,GetWindowLongW(window,GWL_STYLE),FALSE,0,HIWORD(wp));SetWindowPos(window,nullptr,r->left,r->top,size.right-size.left,size.bottom-size.top,SWP_NOZORDER|SWP_NOACTIVATE);v->Fonts();v->Layout();return 0;}
  if(message==WM_TIMER&&v->worker&&WaitForSingleObject(v->worker,0)==WAIT_OBJECT_0){
    KillTimer(window,1);DWORD code=ERROR_GEN_FAILURE;GetExitCodeProcess(v->worker,&code);CloseHandle(v->worker);v->worker=nullptr;v->Complete(code);return 0;}
  if(message==WM_COMMAND){if(LOWORD(wp)==111&&HIWORD(wp)==EN_CHANGE){if(v->path_control){wchar_t path[32768]{};GetWindowTextW(v->path_control,path,32768);v->target=std::filesystem::path(path);}return 0;}if(LOWORD(wp)==114&&HIWORD(wp)==CBN_SELCHANGE){v->schema=uint32_t(SendMessageW(v->schema_box,CB_GETCURSEL,0,0));return 0;}switch(LOWORD(wp)){
    case primary_id:if(v->page==2||v->startup_blocked)DestroyWindow(window);else if(v->page!=1)v->Start();return 0;
    case secondary_id:if(v->page!=1)DestroyWindow(window);return 0;
    case browse_id:v->Browse();return 0;
    case settings_id:{const auto runtime=v->Runtime();if(FAILED(ValidateMachineRuntime(runtime))){v->configuration_failed=true;v->result=ERROR_ACCESS_DENIED;v->status=L"系统组件验证未通过，请查看诊断。";v->Refresh();return 0;}auto exe=runtime/L"YANVY-Settings.exe";ShellExecuteW(window,L"open",exe.c_str(),nullptr,runtime.c_str(),SW_SHOWNORMAL);return 0;}
    case notes_id:{WNDCLASSW wc{};wc.hInstance=v->instance;wc.lpfnWndProc=NotesProc;wc.lpszClassName=L"YANVY.Setup.Notes";wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);RegisterClassW(&wc);
      v->show_diagnostics=v->NeedsDiagnostic();
      auto notes=CreateWindowExW(0,wc.lpszClassName,v->show_diagnostics?L"言舟 · 本机诊断（仅显示，不上传）":L"言舟 · 版本与开源说明",WS_OVERLAPPEDWINDOW,v->D(100),v->D(100),v->D(630),v->D(480),window,nullptr,v->instance,v);ShowWindow(notes,SW_SHOWNORMAL);return 0;}
  }}
  if(message==WM_CLOSE){if(v->page!=1)DestroyWindow(window);return 0;}
  if(message==WM_DESTROY){PostQuitMessage(0);return 0;}
  return DefWindowProcW(window,message,wp,lp);
}
}
int InstallerUI(HINSTANCE instance,bool uninstall,bool preview,int show,DWORD startup_error){
  SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
  INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_PROGRESS_CLASS|ICC_STANDARD_CLASSES};InitCommonControlsEx(&controls);
  View view;view.instance=instance;view.uninstall=uninstall;view.preview=preview;if(uninstall)view.target=ModuleDir();
  else{
    if(ReadPreviousInstallation(view.previous_install,view.previous_data)==ERROR_SUCCESS&&!view.previous_install.empty()){
      view.schema=ReadSchemaPreference(PreferenceRoot(view.previous_install));
      PackageManifest manifest;const bool reusable=SameMachinePath(view.previous_install,view.Runtime())&&SUCCEEDED(ValidateMachineRuntime(view.previous_install))&&ValidInstallPath(view.previous_data)&&LoadPackageManifest(instance,manifest)&&InspectPackageDestination(view.previous_data,manifest,true)==PackageDestination::reusable;
      view.target=SelectExistingInstallData(view.target,view.previous_install,view.Runtime(),view.previous_data,reusable);}}
  if(startup_error){view.startup_blocked=true;view.page=3;view.result=startup_error;view.diagnostic_phase=InstallerPhase::startup;view.status=view.FailureText();}
  WNDCLASSW wc{};wc.hInstance=instance;wc.lpfnWndProc=Proc;wc.lpszClassName=L"YANVY.Setup.rc1";wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);wc.hIcon=LoadIconW(instance,MAKEINTRESOURCEW(1));RegisterClassW(&wc);
  UINT dpi=FitSurfaceDpi(nullptr,GetDpiForSystem(),800,560);RECT rect{0,0,MulDiv(800,int(dpi),96),MulDiv(560,int(dpi),96)};DWORD style=WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX;AdjustWindowRectExForDpi(&rect,style,FALSE,0,dpi);
  HWND window=CreateWindowExW(WS_EX_CONTROLPARENT,wc.lpszClassName,uninstall?L"言舟 · 卸载":L"言舟 · 安装",style,CW_USEDEFAULT,CW_USEDEFAULT,rect.right-rect.left,rect.bottom-rect.top,nullptr,nullptr,instance,&view);
  if(!window)return 1;
  CenterSurface(window);ShowWindow(window,show);MSG message;
  while(GetMessageW(&message,nullptr,0,0)>0)if(!IsDialogMessageW(window,&message)){TranslateMessage(&message);DispatchMessageW(&message);}
  return 0;
}
int RenderInstaller(HINSTANCE instance,const std::filesystem::path& output,UINT dpi,int page){
  if(dpi<96||dpi>288||page<0||page>4||std::filesystem::exists(output))return 2;
  View v;v.instance=instance;v.dpi=dpi;v.page=page==4?0:page;v.uninstall=page==4;v.Fonts();v.result=page==3?kPackageMismatch:0;v.status=page==3?v.FailureText():page==2?L"任务已完成。":page==1?L"正在处理，请稍候…":page==4?L"卸载将请求 Windows 管理员授权。":L"安装包未签名。安装系统组件时，将请求管理员授权。";
  HDC screen=GetDC(nullptr),dc=CreateCompatibleDC(screen);HBITMAP bitmap=CreateCompatibleBitmap(screen,v.D(800),v.D(560));auto old=SelectObject(dc,bitmap);v.Draw(dc,false);SelectObject(dc,old);DeleteDC(dc);ReleaseDC(nullptr,screen);
  int result=1;{Gdiplus::Bitmap png(bitmap,nullptr);CLSID encoder{0x557cf406,0x1a04,0x11d3,{0x9a,0x73,0x00,0x00,0xf8,0x1e,0xf3,0x2e}};if(png.Save(output.c_str(),&encoder,nullptr)==Gdiplus::Ok)result=0;}DeleteObject(bitmap);return result;
}
}
