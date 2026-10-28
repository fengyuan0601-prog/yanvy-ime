// SPDX-License-Identifier: BSD-3-Clause
// The public setup UI stays unelevated. Only an explicit install/uninstall
// action starts the elevated, embedded offline transaction.
#include "installer_ui.hpp"
#include "appearance.hpp"
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
std::filesystem::path DefaultInstall(){
  auto preferred=std::filesystem::path(L"E:\\Codex\\Programs");
  if(GetDriveTypeW(L"E:\\")==DRIVE_FIXED&&std::filesystem::is_directory(preferred))return SelectDefaultInstall(true,{});
  PWSTR local=nullptr;std::filesystem::path result;
  if(SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData,0,nullptr,&local))){result=SelectDefaultInstall(false,std::filesystem::path(local));CoTaskMemFree(local);}
  return result;
}
constexpr int primary_id=101,secondary_id=102,browse_id=103,notes_id=104,settings_id=105;
struct View {
  HINSTANCE instance{};HWND window{},primary{},secondary{},browse{},notes{},settings{},progress{},heading{},status_control{},path_control{};
  HWND schema_box{};uint32_t schema=1;
  UINT dpi=96;int page=0;bool uninstall=false,preview=false;DWORD result=0;HANDLE worker=nullptr;
  HFONT body{},caption_font{},headline{},brand{},strong{};HICON icon{};ULONG_PTR graphics{};
  HBRUSH white=CreateSolidBrush(RGB(248,250,252)),card=CreateSolidBrush(RGB(253,254,255));
  std::filesystem::path target=DefaultInstall();std::wstring status;
  View(){Gdiplus::GdiplusStartupInput input;input.SuppressExternalCodecs=TRUE;Gdiplus::GdiplusStartup(&graphics,&input,nullptr);}
  ~View(){ClearFonts();DeleteObject(white);DeleteObject(card);if(worker)CloseHandle(worker);if(graphics)Gdiplus::GdiplusShutdown(graphics);}
  int D(int n)const{return MulDiv(n,int(dpi),96);}
  RECT R(int l,int t,int r,int b)const{return {D(l),D(t),D(r),D(b)};}
  void ClearFonts(){for(auto font:{body,caption_font,headline,brand,strong})if(font)DeleteObject(font);}
  void Fonts(){ClearFonts();body=Font(D(14));caption_font=Font(D(12));headline=Font(D(27),FW_SEMIBOLD);brand=Font(D(30),FW_SEMIBOLD);strong=Font(D(15),FW_SEMIBOLD);
    icon=static_cast<HICON>(LoadImageW(instance,MAKEINTRESOURCEW(1),IMAGE_ICON,D(56),D(56),LR_SHARED));}
  const wchar_t* Title()const{return page==1?(uninstall?L"正在卸载言舟":L"正在安装言舟"):page==2?(uninstall?L"已完成卸载":L"安装完成，开始落笔"):page==3?L"暂时未能完成":uninstall?L"卸载言舟":L"安装言舟，轻松落笔";}
  const wchar_t* Primary()const{return page==1?L"请稍候…":page==2?L"完成":page==3?L"重试":uninstall?L"确认卸载":L"安装言舟";}
  void Text(HDC dc,const wchar_t* text,RECT rect,HFONT font,COLORREF color,bool wrap=false){
    if(!wrap){Label(dc,text,rect,font,color);return;}
    auto old=SelectObject(dc,font);SetBkMode(dc,TRANSPARENT);SetTextColor(dc,color);
    DrawTextW(dc,text,-1,&rect,DT_LEFT|DT_WORDBREAK|DT_NOPREFIX);SelectObject(dc,old);
  }
  void Button(HDC dc,RECT rect,const wchar_t* text,bool main,bool disabled=false,bool focus=false,bool pressed=false){
    auto p=Palette::Get(false);auto fill=main?(disabled?RGB(212,226,232):pressed?RGB(8,85,88):p.accent):p.background;
    Rounded(dc,rect,D(8),fill,main?fill:p.border);Label(dc,text,rect,strong,main?(disabled?p.muted:RGB(255,255,255)):p.text,DT_CENTER);
    if(focus){InflateRect(&rect,-D(4),-D(4));DrawFocusRect(dc,&rect);}
  }
  void Draw(HDC dc,bool live){
    auto p=Palette::Get(false);Fill(dc,R(0,0,720,480),RGB(248,250,252));Fill(dc,R(0,0,200,480),RGB(17,48,65));
    if(icon)DrawIconEx(dc,D(28),D(30),icon,D(56),D(56),0,nullptr,DI_NORMAL);
    Text(dc,L"言舟",R(28,102,174,146),brand,RGB(244,250,251));Text(dc,L"YANVY",R(30,149,176,175),strong,RGB(141,197,205));
    Text(dc,L"让文字顺畅落下，\n让表达有自己的空间。",R(28,205,178,262),body,RGB(203,222,229),true);
    Rounded(dc,R(20,310,180,386),D(10),RGB(26,65,80),RGB(40,82,94));
    Text(dc,L"离线 · 本机转换",R(32,322,172,346),strong,RGB(162,230,217));
    Text(dc,L"免费开源，无广告",R(32,351,172,374),caption_font,RGB(201,222,228));
    Text(dc,L"一念开物 · IDEORIN",R(28,431,188,452),caption_font,RGB(158,189,202));
    Text(dc,uninstall?L"卸载向导":L"安装向导",R(232,25,395,48),caption_font,p.muted);
    Label(dc,kVersion,R(490,25,688,48),caption_font,p.muted,DT_RIGHT);
    if(!live)Text(dc,Title(),R(232,65,690,106),headline,p.text);
    const wchar_t* subtitle=page==1?L"正在处理本地组件，请保持此窗口打开。":page==2?(uninstall?L"其他输入法和保留的设置不受影响。":L"言舟已就绪，重开应用后即可选择。"):
        page==3?L"请查看下方原因，调整后可以重新尝试。":uninstall?L"移除程序与输入法注册，保留外观设置和未知文件。":L"设置和缓存优先保存在可写的 E 盘，否则使用当前用户目录。";
    Text(dc,subtitle,R(232,116,688,162),body,p.muted,true);
    if(page==0||page==3){
      Rounded(dc,R(232,179,688,266),D(11),p.background,p.border);
      Text(dc,uninstall?L"当前安装位置":L"安装位置 · 可直接输入完整路径",R(248,190,620,211),caption_font,p.muted);
      if(!live)Text(dc,target.c_str(),R(248,220,uninstall?670:611,248),body,p.text);
      if(!live&&!uninstall)Button(dc,R(619,217,676,251),L"更改",false);
      Text(dc,uninstall?L"请先切换到其他输入法，并关闭使用言舟的应用。":L"默认输入方案",R(232,286,uninstall?688:460,313),body,p.text);
      if(!live&&!uninstall){Rounded(dc,R(484,282,688,317),D(5),p.background,p.border);Text(dc,ime::kSchemaLabels[schema],R(495,285,652,314),body,p.text);Text(dc,L"▾",R(660,285,681,314),body,p.muted);}
      Text(dc,uninstall?L"你的外观设置和未列入清单的文件将保留。":L"✓  保留其他输入法；外观设置可迁移",R(232,324,688,350),body,p.text);
    }else if(page==1){
      Rounded(dc,R(232,179,688,318),D(11),p.background,p.border);
      Text(dc,uninstall?L"注销输入法并移除已列明的程序文件。":L"解压固定词库、校验组件并注册输入法。",R(250,197,670,231),strong,p.text,true);
      if(!live){Fill(dc,R(250,260,670,266),RGB(226,235,240));Rounded(dc,R(250,260,380,266),D(3),p.accent,p.accent);}
      Text(dc,L"实际耗时取决于词库部署与系统响应。",R(250,285,670,307),caption_font,p.muted);
    }else if(page==2){
      Rounded(dc,R(232,179,688,333),D(11),p.background,p.border);
      Text(dc,L"✓",R(252,195,300,233),headline,p.accent);
      Text(dc,uninstall?L"已移除言舟的输入法注册":L"接下来，只需三步",R(298,199,665,229),strong,p.text);
      Text(dc,uninstall?L"被应用占用的文件可能在重启后清理。\n其余输入方式和手动创建的文件已保留。":L"1   保存工作，重新打开要输入的应用\n2   在系统输入法切换器中选择言舟\n3   开始输入；外观可在言舟设置中调整",R(252,244,668,320),body,p.text,true);
    }
    if(!live&&!status.empty())Text(dc,status.c_str(),R(232,353,688,394),caption_font,page==3?RGB(165,58,44):p.muted,true);
    Fill(dc,R(232,403,688,404),p.line);
    if(!live){Text(dc,L"版本与开源说明",R(232,426,408,452),caption_font,p.accent);if(page!=1&&page!=2)Button(dc,R(442,421,532,461),L"取消",false);
      if(page==2&&!uninstall)Button(dc,R(408,421,532,461),L"打开设置",false);
      Button(dc,R(548,421,688,461),Primary(),true,page==1);}
  }
  void Layout(){
    auto place=[&](HWND control,RECT r){SetWindowPos(control,nullptr,r.left,r.top,r.right-r.left,r.bottom-r.top,SWP_NOZORDER|SWP_NOACTIVATE);};
    place(heading,R(232,65,690,106));SendMessageW(heading,WM_SETFONT,reinterpret_cast<WPARAM>(headline),TRUE);
    place(path_control,R(248,220,uninstall?670:611,249));SendMessageW(path_control,WM_SETFONT,reinterpret_cast<WPARAM>(body),TRUE);
    place(status_control,R(232,353,688,394));SendMessageW(status_control,WM_SETFONT,reinterpret_cast<WPARAM>(caption_font),TRUE);
    place(primary,R(548,421,688,461));place(secondary,R(442,421,532,461));place(browse,R(619,217,676,251));place(schema_box,R(484,282,688,440));
    SendMessageW(schema_box,WM_SETFONT,reinterpret_cast<WPARAM>(body),TRUE);SendMessageW(schema_box,CB_SETITEMHEIGHT,WPARAM(-1),D(28));
    place(notes,R(232,421,405,461));place(settings,R(408,421,532,461));place(progress,R(250,259,670,267));
    for(auto control:{primary,secondary,browse,settings,notes})SendMessageW(control,WM_SETFONT,reinterpret_cast<WPARAM>(strong),TRUE);
    Refresh();
  }
  void Refresh(){
    SetWindowTextW(heading,Title());SetWindowTextW(primary,Primary());SetWindowTextW(status_control,status.c_str());SetWindowTextW(path_control,target.c_str());
    EnableWindow(primary,page!=1);ShowWindow(secondary,page!=1&&page!=2?SW_SHOW:SW_HIDE);ShowWindow(settings,page==2&&!uninstall?SW_SHOW:SW_HIDE);
    ShowWindow(browse,(page==0||page==3)&&!uninstall?SW_SHOW:SW_HIDE);ShowWindow(path_control,page==0||page==3?SW_SHOW:SW_HIDE);
    ShowWindow(schema_box,(page==0||page==3)&&!uninstall?SW_SHOW:SW_HIDE);SendMessageW(schema_box,CB_SETCURSEL,schema,0);
    ShowWindow(progress,page==1?SW_SHOW:SW_HIDE);SendMessageW(progress,PBM_SETMARQUEE,page==1,35);
    EnableWindow(notes,page!=1);InvalidateRect(window,nullptr,FALSE);
  }
  void Browse(){
    Microsoft::WRL::ComPtr<IFileOpenDialog> dialog;
    if(FAILED(CoCreateInstance(CLSID_FileOpenDialog,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&dialog))))return;
    dialog->SetOptions(FOS_PICKFOLDERS|FOS_FORCEFILESYSTEM|FOS_PATHMUSTEXIST|FOS_DONTADDTORECENT);dialog->SetTitle(L"选择安装目录（非空目录将创建言舟版本子目录）");
    auto parent=target.parent_path().parent_path();Microsoft::WRL::ComPtr<IShellItem> start;if(SUCCEEDED(SHCreateItemFromParsingName(parent.c_str(),nullptr,IID_PPV_ARGS(&start))))dialog->SetFolder(start.Get());
    if(SUCCEEDED(dialog->Show(window))){Microsoft::WRL::ComPtr<IShellItem> item;PWSTR value=nullptr;
      if(SUCCEEDED(dialog->GetResult(&item))&&SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH,&value))){auto selected=std::filesystem::path(value);CoTaskMemFree(value);
        std::error_code error;if(!std::filesystem::is_empty(selected,error)||error)selected=ResolveInstallPath(selected);
        if(ValidInstallPath(selected)){target=selected;status=L"可直接修改上方完整路径；设置和缓存单独保存。";}else status=L"请选择本地普通文件夹，不使用网络路径或链接。";Refresh();}}
  }
  void Start(){
    if(preview){status=L"界面预览不执行安装，也不更改系统输入法。";Refresh();return;}
    if(!ValidInstallPath(target)){status=L"安装位置无效：请选择本地普通文件夹。";page=3;Refresh();return;}
    if(!uninstall){std::error_code error;if(std::filesystem::exists(target,error)&&(!std::filesystem::is_directory(target,error)||!std::filesystem::is_empty(target,error))){status=L"此目录已有文件。请更改位置，选择新的空目录。";page=3;Refresh();return;}}
    wchar_t exe[32768]{};GetModuleFileNameW(nullptr,exe,32768);
    std::wstring arguments=std::wstring(uninstall?L"--worker-uninstall ":L"--worker-install ")+L"\""+target.wstring()+L"\"";
    if(!uninstall)arguments+=L" "+std::to_wstring(schema);
    SHELLEXECUTEINFOW launch{sizeof(launch)};launch.fMask=SEE_MASK_NOCLOSEPROCESS|SEE_MASK_NOASYNC|SEE_MASK_FLAG_NO_UI;
    launch.hwnd=window;launch.lpVerb=L"runas";launch.lpFile=exe;launch.lpParameters=arguments.c_str();launch.nShow=SW_HIDE;
    if(!ShellExecuteExW(&launch)||!launch.hProcess){result=GetLastError();status=result==ERROR_CANCELLED?L"已取消管理员授权，尚未开始安装或卸载。":L"无法启动安装任务，请重新打开安装包后重试。";Refresh();return;}
    worker=launch.hProcess;page=1;status=L"正在处理，请稍候…";Refresh();SetTimer(window,1,200,nullptr);
  }
  bool ApplySelectedSchema(){
    auto executable=target/L"YANVY-Settings.exe";
    std::wstring command=L"\""+executable.wstring()+L"\" --apply-schema "+std::to_wstring(schema);
    STARTUPINFOW startup{sizeof(startup)};PROCESS_INFORMATION process{};
    if(!CreateProcessW(executable.c_str(),command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,target.c_str(),&startup,&process))return false;
    CloseHandle(process.hThread);DWORD code=1;DWORD waited=WaitForSingleObject(process.hProcess,5000);
    if(waited==WAIT_OBJECT_0)GetExitCodeProcess(process.hProcess,&code);
    CloseHandle(process.hProcess);return waited==WAIT_OBJECT_0&&code==0;
  }
};
LRESULT CALLBACK NotesProc(HWND window,UINT message,WPARAM wp,LPARAM lp){
  if(message==WM_CREATE){auto view=static_cast<View*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(view));
    auto data=FindResourceW(view->instance,MAKEINTRESOURCEW(102),RT_RCDATA);std::wstring text;
    if(data){auto bytes=static_cast<const char*>(LockResource(LoadResource(view->instance,data)));auto raw=Wide(std::string(bytes,SizeofResource(view->instance,data)));if(!raw.empty()&&raw.front()==0xfeff)raw.erase(0,1);
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
  if(message==WM_CREATE){v->dpi=GetDpiForWindow(window);v->Fonts();
    auto control=[&](const wchar_t* cls,const wchar_t* text,DWORD style,int id){return CreateWindowExW(0,cls,text,WS_CHILD|WS_VISIBLE|style,0,0,1,1,window,reinterpret_cast<HMENU>(INT_PTR(id)),v->instance,nullptr);};
    v->heading=control(L"STATIC",L"",SS_LEFT|SS_CENTERIMAGE,110);v->path_control=control(L"EDIT",L"",ES_AUTOHSCROLL|WS_TABSTOP|(v->uninstall?ES_READONLY:0),111);
    v->status_control=control(L"STATIC",L"",SS_LEFT,112);v->primary=control(L"BUTTON",L"",BS_OWNERDRAW|WS_TABSTOP,primary_id);
    v->schema_box=control(L"COMBOBOX",L"默认输入方案",CBS_DROPDOWNLIST|WS_TABSTOP,114);for(auto text:ime::kSchemaLabels)SendMessageW(v->schema_box,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(text));
    v->secondary=control(L"BUTTON",L"取消",BS_OWNERDRAW|WS_TABSTOP,secondary_id);v->browse=control(L"BUTTON",L"更改",BS_OWNERDRAW|WS_TABSTOP,browse_id);
    v->notes=control(L"BUTTON",L"版本与开源说明",BS_OWNERDRAW|WS_TABSTOP,notes_id);v->settings=control(L"BUTTON",L"打开设置",BS_OWNERDRAW|WS_TABSTOP,settings_id);
    v->progress=control(PROGRESS_CLASSW,L"安装进度",PBS_MARQUEE,113);v->status=v->preview?L"界面预览 · 不执行安装或更改系统注册":v->uninstall?L"卸载将请求 Windows 管理员授权。":L"候选版 · 未签名；安装时将请求管理员授权。";v->Layout();return 0;
  }
  if(message==WM_ERASEBKGND)return 1;
  if(message==WM_PAINT){PAINTSTRUCT ps{};HDC dc=BeginPaint(window,&ps);RECT rect{};GetClientRect(window,&rect);auto buffer=CreateCompatibleDC(dc);auto bitmap=CreateCompatibleBitmap(dc,rect.right,rect.bottom);auto old=SelectObject(buffer,bitmap);v->Draw(buffer,true);BitBlt(dc,0,0,rect.right,rect.bottom,buffer,0,0,SRCCOPY);SelectObject(buffer,old);DeleteObject(bitmap);DeleteDC(buffer);EndPaint(window,&ps);return 0;}
  if(message==WM_CTLCOLORSTATIC||message==WM_CTLCOLOREDIT){auto dc=reinterpret_cast<HDC>(wp);SetBkMode(dc,TRANSPARENT);SetTextColor(dc,reinterpret_cast<HWND>(lp)==v->status_control?(v->page==3?RGB(165,58,44):RGB(94,116,133)):RGB(22,44,62));return reinterpret_cast<LRESULT>(reinterpret_cast<HWND>(lp)==v->path_control?v->card:v->white);}
  if(message==WM_DRAWITEM){auto& item=*reinterpret_cast<DRAWITEMSTRUCT*>(lp);wchar_t text[80]{};GetWindowTextW(item.hwndItem,text,80);
    if(item.CtlID==notes_id){Fill(item.hDC,item.rcItem,RGB(248,250,252));Label(item.hDC,text,item.rcItem,v->caption_font,RGB(12,109,108));if(item.itemState&ODS_FOCUS){RECT r=item.rcItem;InflateRect(&r,-v->D(3),-v->D(3));DrawFocusRect(item.hDC,&r);}}
    else {Fill(item.hDC,item.rcItem,item.CtlID==browse_id?RGB(253,254,255):RGB(248,250,252));v->Button(item.hDC,item.rcItem,text,item.CtlID==primary_id,(item.itemState&ODS_DISABLED)!=0,(item.itemState&ODS_FOCUS)!=0,(item.itemState&ODS_SELECTED)!=0);}
    return TRUE;}
  if(message==WM_DPICHANGED){v->dpi=HIWORD(wp);auto r=reinterpret_cast<RECT*>(lp);SetWindowPos(window,nullptr,r->left,r->top,r->right-r->left,r->bottom-r->top,SWP_NOZORDER|SWP_NOACTIVATE);v->Fonts();v->Layout();return 0;}
  if(message==WM_TIMER&&v->worker&&WaitForSingleObject(v->worker,0)==WAIT_OBJECT_0){KillTimer(window,1);GetExitCodeProcess(v->worker,&v->result);CloseHandle(v->worker);v->worker=nullptr;
    v->page=(v->result==0||v->result==3010||v->result==33)?2:3;
    bool scheme_ok=v->page!=2||v->uninstall||v->ApplySelectedSchema();
    if(v->page==2&&!v->uninstall)StartBroker(v->target);
    v->status=v->page==2?(v->result==3010?L"旧版被占用的文件将在重启后清理；不会自动重启。":v->result==33?L"新版已安装，但旧版目录无法安全验证或清理，请保留旧目录并反馈。":L"任务已完成。"):
      v->result==10?L"目录已有文件，或不是有效的本地安装位置。":v->result==20?L"安装文件处理失败（20），请检查磁盘空间和权限。":
      v->result==21?L"安装文件解压失败（21），请检查磁盘空间和权限。":v->result==22?L"无法启动词库检查程序（22），请检查安全软件和系统权限。":
      v->result==23?L"引擎组件或系统依赖无法加载（23），请检查安全软件并反馈此代码。":v->result==24?L"词库初始化或校验失败（24），请反馈此代码。":v->result==25?L"词库没有返回预期候选（25），请反馈此代码。":
      v->result==26?L"词库检查程序异常退出（26），请检查系统组件和安全软件。":v->result==27?L"无法访问词库目录（27），请检查位置和权限。":
      v->result==28?L"安装路径包含链接或重解析点（28），请选择普通本地目录。":v->result==29?L"词库文件不完整或校验不匹配（29），请重新获取安装包。":
      v->result==32?L"词库部署或配置校验失败（32），请反馈此代码。":v->result==30?(v->uninstall?L"Windows 未完成注销，程序文件已保留。请关闭使用言舟的应用后重试。":L"系统注册失败；本次安装已回退。"):
      v->result==31?L"安装失败，回退未完成。新旧程序文件已保留，请保留目录并报告错误类别 31。":v->result==ERROR_BUSY?L"已有安装或卸载任务正在运行，请稍后再试。":L"操作未完成。错误类别："+std::to_wstring(v->result);
    if(v->result>=0x10000u){
      const DWORD error=(v->result&0xffff0000u)==0xa1260000u ? v->result&0xffffu : v->result;
      wchar_t code[11]{};swprintf(code,11,L"0x%08lX",static_cast<unsigned long>(error));
      v->status=std::wstring(L"安装异常，错误码 ")+code+L"，请反馈。";
    }
    if(!scheme_ok)v->status+=L" 输入方案未保存，请打开设置重试。";
    v->Refresh();return 0;}
  if(message==WM_COMMAND){if(LOWORD(wp)==111&&HIWORD(wp)==EN_CHANGE){if(v->path_control){wchar_t path[32768]{};GetWindowTextW(v->path_control,path,32768);v->target=std::filesystem::path(path);}return 0;}if(LOWORD(wp)==114&&HIWORD(wp)==CBN_SELCHANGE){v->schema=uint32_t(SendMessageW(v->schema_box,CB_GETCURSEL,0,0));return 0;}switch(LOWORD(wp)){
    case primary_id:if(v->page==2)DestroyWindow(window);else if(v->page!=1)v->Start();return 0;
    case secondary_id:if(v->page!=1)DestroyWindow(window);return 0;
    case browse_id:v->Browse();return 0;
    case settings_id:{auto exe=v->target/L"YANVY-Settings.exe";ShellExecuteW(window,L"open",exe.c_str(),nullptr,v->target.c_str(),SW_SHOWNORMAL);return 0;}
    case notes_id:{WNDCLASSW wc{};wc.hInstance=v->instance;wc.lpfnWndProc=NotesProc;wc.lpszClassName=L"YANVY.Setup.Notes";wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);RegisterClassW(&wc);
      auto notes=CreateWindowExW(0,wc.lpszClassName,L"言舟 · 版本与开源说明",WS_OVERLAPPEDWINDOW,v->D(100),v->D(100),v->D(630),v->D(480),window,nullptr,v->instance,v);ShowWindow(notes,SW_SHOWNORMAL);return 0;}
  }}
  if(message==WM_CLOSE){if(v->page!=1)DestroyWindow(window);return 0;}
  if(message==WM_DESTROY){PostQuitMessage(0);return 0;}
  return DefWindowProcW(window,message,wp,lp);
}
}
int InstallerUI(HINSTANCE instance,bool uninstall,bool preview,int show){
  SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
  INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_PROGRESS_CLASS|ICC_STANDARD_CLASSES};InitCommonControlsEx(&controls);
  View view;view.instance=instance;view.uninstall=uninstall;view.preview=preview;if(uninstall)view.target=ModuleDir();
  else{wchar_t previous[32768]{};DWORD bytes=sizeof(previous);
    if(RegGetValueW(HKEY_LOCAL_MACHINE,L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\YANVY-dev",L"InstallLocation",RRF_RT_REG_SZ|RRF_SUBKEY_WOW6464KEY,nullptr,previous,&bytes)==ERROR_SUCCESS)
      view.schema=ReadSchemaPreference(PreferenceRoot(std::filesystem::path(previous)));}
  WNDCLASSW wc{};wc.hInstance=instance;wc.lpfnWndProc=Proc;wc.lpszClassName=L"YANVY.Setup.rc1";wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);wc.hIcon=LoadIconW(instance,MAKEINTRESOURCEW(1));RegisterClassW(&wc);
  UINT dpi=GetDpiForSystem();RECT rect{0,0,MulDiv(720,int(dpi),96),MulDiv(480,int(dpi),96)};DWORD style=WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX;AdjustWindowRectExForDpi(&rect,style,FALSE,0,dpi);
  HWND window=CreateWindowExW(WS_EX_CONTROLPARENT,wc.lpszClassName,uninstall?L"言舟 · 卸载":L"言舟 · 安装",style,CW_USEDEFAULT,CW_USEDEFAULT,rect.right-rect.left,rect.bottom-rect.top,nullptr,nullptr,instance,&view);
  if(!window)return 1;
  ShowWindow(window,show);MSG message;
  while(GetMessageW(&message,nullptr,0,0)>0)if(!IsDialogMessageW(window,&message)){TranslateMessage(&message);DispatchMessageW(&message);}
  return 0;
}
int RenderInstaller(HINSTANCE instance,const std::filesystem::path& output,UINT dpi,int page){
  if(dpi<96||dpi>288||page<0||page>4||std::filesystem::exists(output))return 2;
  View v;v.instance=instance;v.dpi=dpi;v.page=page==4?0:page;v.uninstall=page==4;v.Fonts();v.status=page==3?L"目录已有文件。请更改位置，选择新的空目录。":page==2?L"任务已完成。":page==1?L"正在处理，请稍候…":page==4?L"卸载将请求 Windows 管理员授权。":L"候选版 · 未签名；安装时将请求管理员授权。";
  HDC screen=GetDC(nullptr),dc=CreateCompatibleDC(screen);HBITMAP bitmap=CreateCompatibleBitmap(screen,v.D(720),v.D(480));auto old=SelectObject(dc,bitmap);v.Draw(dc,false);SelectObject(dc,old);DeleteDC(dc);ReleaseDC(nullptr,screen);
  int result=1;{Gdiplus::Bitmap png(bitmap,nullptr);CLSID encoder{0x557cf406,0x1a04,0x11d3,{0x9a,0x73,0x00,0x00,0xf8,0x1e,0xf3,0x2e}};if(png.Save(output.c_str(),&encoder,nullptr)==Gdiplus::Ok)result=0;}DeleteObject(bitmap);return result;
}
}
