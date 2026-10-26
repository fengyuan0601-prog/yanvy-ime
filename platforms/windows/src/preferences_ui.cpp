// SPDX-License-Identifier: BSD-3-Clause
#include "preferences_ui.hpp"
#include "candidate_view.hpp"
#include <commctrl.h>
#include <shellapi.h>
#include <objbase.h>

namespace yanvy {
namespace {
enum {NavInput=101,NavAppearance,NavAbout,SaveButton,ResetButton,HelpButton,
      PinyinRadio,WubiRadio,FullRadio,DoubleRadio,DoubleBox,FontBox,ThemeBox};
struct Preferences {
  Appearance display;uint32_t schema=1,pinyin=1,double_schema=2;
  bool Equals(const Preferences& other)const{return display.font_size==other.display.font_size&&display.theme==other.display.theme&&schema==other.schema&&pinyin==other.pinyin&&double_schema==other.double_schema;}
};
bool SavePreferences(const std::filesystem::path& ini,const Preferences& value,bool apply_input=false){
  GUID guid{};wchar_t name[64]{};if(FAILED(CoCreateGuid(&guid)))return false;StringFromGUID2(guid,name,64);
  auto temporary=ini.parent_path()/(std::wstring(L"settings-pending-")+name+L".ini");
  if(!CopyFileW(ini.c_str(),temporary.c_str(),TRUE)&&GetLastError()!=ERROR_FILE_NOT_FOUND)return false;
  auto size=std::to_wstring(value.display.font_size),theme=std::to_wstring(value.display.theme),schema=std::to_wstring(value.schema),pinyin=std::to_wstring(value.pinyin),double_schema=std::to_wstring(value.double_schema);
  bool ok=WritePrivateProfileStringW(L"display",L"font_size",size.c_str(),temporary.c_str())&&
      WritePrivateProfileStringW(L"display",L"theme",theme.c_str(),temporary.c_str())&&
      WritePrivateProfileStringW(L"input",L"schema",schema.c_str(),temporary.c_str())&&
      WritePrivateProfileStringW(L"input",L"pinyin_schema",pinyin.c_str(),temporary.c_str())&&WritePrivateProfileStringW(L"input",L"double_schema",double_schema.c_str(),temporary.c_str());
  if(ok&&apply_input)ok=WritePrivateProfileStringW(L"input",L"change_id",name,temporary.c_str())!=FALSE;
  if(ok)ok=MoveFileExW(temporary.c_str(),ini.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=FALSE;
  if(!ok)DeleteFileW(temporary.c_str());
  WritePrivateProfileStringW(nullptr,nullptr,nullptr,ini.c_str());return ok;
}
struct View {
  HINSTANCE instance{};HWND window{},status_window{};UINT dpi=96;int page=0;
  HFONT body{},title{},caption{},strong{};HICON icon{};HBRUSH background=CreateSolidBrush(RGB(245,248,251)),card=CreateSolidBrush(RGB(253,254,255));
  Preferences saved,current;CandidateView preview;std::wstring status=L"先选择拼音或五笔，再选择具体输入方式。";
  std::vector<HWND> input_controls,appearance_controls;HWND pinyin_radio{},wubi_radio{},full_radio{},double_radio{},double_box{},font_box{},theme_box{},save{};
  ~View(){ClearFonts();DeleteObject(background);DeleteObject(card);}
  int D(int n)const{return MulDiv(n,int(dpi),96);}RECT R(int l,int t,int r,int b)const{return {D(l),D(t),D(r),D(b)};}
  void ClearFonts(){for(auto font:{body,title,caption,strong})if(font)DeleteObject(font);}
  void Fonts(){ClearFonts();body=Font(D(15));title=Font(D(26),FW_SEMIBOLD);caption=Font(D(12));strong=Font(D(15),FW_SEMIBOLD);
    icon=static_cast<HICON>(LoadImageW(instance,MAKEINTRESOURCEW(1),IMAGE_ICON,D(36),D(36),LR_SHARED));}
  HWND Control(const wchar_t* cls,const wchar_t* text,DWORD style,int id,RECT rect){
    auto control=CreateWindowExW(0,cls,text,WS_CHILD|WS_VISIBLE|style,rect.left,rect.top,rect.right-rect.left,rect.bottom-rect.top,window,reinterpret_cast<HMENU>(INT_PTR(id)),instance,nullptr);
    SendMessageW(control,WM_SETFONT,reinterpret_cast<WPARAM>(body),TRUE);return control;
  }
  void Text(HDC dc,const wchar_t* text,RECT rect,HFONT font,COLORREF color,bool wrap=false){
    if(!wrap){Label(dc,text,rect,font,color);return;}auto old=SelectObject(dc,font);SetBkMode(dc,TRANSPARENT);SetTextColor(dc,color);DrawTextW(dc,text,-1,&rect,DT_LEFT|DT_WORDBREAK|DT_NOPREFIX);SelectObject(dc,old);
  }
  void UpdatePreview(){Response response;response.count=1;wcscpy_s(response.input,ime::kSchemaExamples[current.schema]);wcscpy_s(response.preedit,ime::kSchemaExamples[current.schema]);wcscpy_s(response.candidates[0],L"你好");preview.Set(response,current.schema,false,current.display,dpi,icon);}
  void Sync(){
    SendMessageW(pinyin_radio,BM_SETCHECK,current.schema?BST_CHECKED:BST_UNCHECKED,0);SendMessageW(wubi_radio,BM_SETCHECK,current.schema?BST_UNCHECKED:BST_CHECKED,0);
    SendMessageW(full_radio,BM_SETCHECK,current.pinyin==1?BST_CHECKED:BST_UNCHECKED,0);SendMessageW(double_radio,BM_SETCHECK,current.pinyin>1?BST_CHECKED:BST_UNCHECKED,0);
    SendMessageW(double_box,CB_SETCURSEL,current.double_schema-2,0);SendMessageW(font_box,CB_SETCURSEL,current.display.font_size-12,0);SendMessageW(theme_box,CB_SETCURSEL,current.display.theme,0);
    for(auto control:input_controls)ShowWindow(control,page==0?SW_SHOW:SW_HIDE);
    for(auto control:appearance_controls)ShowWindow(control,page==1?SW_SHOW:SW_HIDE);
    EnableWindow(full_radio,current.schema!=0);EnableWindow(double_radio,current.schema!=0);EnableWindow(double_box,current.schema!=0&&current.pinyin>1);
    EnableWindow(save,page==0||!current.Equals(saved));ShowWindow(GetDlgItem(window,ResetButton),page==2?SW_HIDE:SW_SHOW);SetWindowTextW(status_window,status.c_str());UpdatePreview();InvalidateRect(window,nullptr,FALSE);
  }
  void Changed(){status=L"预览已更新，保存并应用后生效。";Sync();}
  void Layout(){
    auto place=[&](int id,RECT r){auto control=GetDlgItem(window,id);SetWindowPos(control,nullptr,r.left,r.top,r.right-r.left,r.bottom-r.top,SWP_NOZORDER|SWP_NOACTIVATE);SendMessageW(control,WM_SETFONT,reinterpret_cast<WPARAM>(body),TRUE);};
    place(NavInput,R(16,126,156,169));place(NavAppearance,R(16,182,156,225));place(NavAbout,R(16,238,156,281));
    place(PinyinRadio,R(220,132,414,166));place(WubiRadio,R(454,132,668,166));place(FullRadio,R(220,214,414,247));place(DoubleRadio,R(454,214,668,247));
    place(DoubleBox,R(344,302,668,420));place(FontBox,R(516,134,668,420));place(ThemeBox,R(516,204,668,410));
    SendMessageW(double_box,CB_SETITEMHEIGHT,WPARAM(-1),D(28));SendMessageW(font_box,CB_SETITEMHEIGHT,WPARAM(-1),D(28));SendMessageW(theme_box,CB_SETITEMHEIGHT,WPARAM(-1),D(28));
    place(ResetButton,R(202,495,306,533));place(HelpButton,R(318,495,422,533));place(SaveButton,R(542,495,690,533));
    RECT status_rect=R(202,536,690,558);SetWindowPos(status_window,nullptr,status_rect.left,status_rect.top,status_rect.right-status_rect.left,status_rect.bottom-status_rect.top,SWP_NOZORDER|SWP_NOACTIVATE);SendMessageW(status_window,WM_SETFONT,reinterpret_cast<WPARAM>(caption),TRUE);
    Sync();
  }
  void Paint(){
    PAINTSTRUCT paint{};HDC dc=BeginPaint(window,&paint);RECT bounds{};GetClientRect(window,&bounds);HDC buffer=CreateCompatibleDC(dc);auto bitmap=CreateCompatibleBitmap(dc,bounds.right,bounds.bottom);auto old=SelectObject(buffer,bitmap);auto p=Palette::Get(false);
    Fill(buffer,bounds,RGB(245,248,251));Fill(buffer,R(0,0,172,560),RGB(17,48,65));if(icon)DrawIconEx(buffer,D(22),D(26),icon,D(36),D(36),0,nullptr,DI_NORMAL);
    Text(buffer,L"言舟",R(68,25,155,62),strong,RGB(244,250,251));Text(buffer,L"设置",R(24,78,152,103),caption,RGB(172,201,211));Text(buffer,L"默认无学习",R(24,505,154,530),caption,RGB(172,201,211));
    Text(buffer,page==0?L"输入方案":page==1?L"外观":L"关于言舟",R(202,23,524,65),title,p.text);Label(buffer,kVersion,R(530,28,690,58),caption,p.muted,DT_RIGHT);
    Text(buffer,page==0?L"选择后保存并应用，不用记快捷键。":page==1?L"选择喜欢的字号与配色，立即查看示例。":L"免费、开源、离线优先的五笔与拼音输入法。",R(202,70,690,96),caption,p.muted);
    if(page==0){
      Rounded(buffer,R(202,112,690,266),D(11),p.background,p.border);Text(buffer,L"拼音方式",R(220,176,668,201),caption,p.muted);
      Rounded(buffer,R(202,286,690,432),D(11),p.background,p.border);Text(buffer,L"双拼方案",R(220,302,332,333),body,p.text);
      Text(buffer,current.schema==0?L"五笔86：按字形编码输入，保留纯五笔候选。":current.pinyin==1?L"全拼：输入完整拼音，例如 nihao。":L"双拼：使用所选方案的声母、韵母键位组合。",R(220,350,669,377),caption,p.muted);
      auto example=std::wstring(L"输入示例：")+ime::kSchemaExamples[current.schema]+L"  →  你好";Text(buffer,example.c_str(),R(220,386,670,416),strong,p.accent);
      Text(buffer,L"保存后下次输入生效；当前未确认的编码会取消。",R(202,445,690,474),caption,p.muted);
    }else if(page==1){
      Rounded(buffer,R(202,112,690,266),D(11),p.background,p.border);Text(buffer,L"候选字号",R(220,134,500,166),body,p.text);Text(buffer,L"调整候选文字的显示大小",R(220,171,500,194),caption,p.muted);
      Text(buffer,L"候选配色",R(220,204,500,236),body,p.text);Text(buffer,L"外观预览 · 示例文字",R(202,275,690,300),caption,p.muted);
      RECT area=R(202,305,690,481);Rounded(buffer,area,D(11),RGB(234,241,246),RGB(226,234,240));auto size=preview.Measure(buffer,D(458));int x=(area.left+area.right-size.cx)/2,y=(area.top+area.bottom-size.cy)/2;preview.Draw(buffer,{x,y,x+size.cx,y+size.cy});
    }else{
      Rounded(buffer,R(202,112,690,461),D(11),p.background,p.border);Text(buffer,L"让文字顺畅落下",R(224,132,665,170),strong,p.text);
      Text(buffer,L"输入转换与候选生成在本机完成。\n默认不学习，不保存正文日志和剪贴板历史。\n\n当前为 Windows x64 未签名候选版。\n完整跨应用、升级卸载与多屏验收仍待完成。\n\n原创核心与文档采用 BSD-3-Clause。\nWindows 衍生前端和双拼方案保留 GPL 许可。\n完整来源与许可证随安装包提供。",R(224,190,665,435),body,p.muted,true);
    }
    BitBlt(dc,0,0,bounds.right,bounds.bottom,buffer,0,0,SRCCOPY);SelectObject(buffer,old);DeleteObject(bitmap);DeleteDC(buffer);EndPaint(window,&paint);
  }
};
LRESULT CALLBACK Proc(HWND window,UINT message,WPARAM wp,LPARAM lp){
  auto v=reinterpret_cast<View*>(GetWindowLongPtrW(window,GWLP_USERDATA));if(message==WM_NCCREATE){v=static_cast<View*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);v->window=window;SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(v));}if(!v)return DefWindowProcW(window,message,wp,lp);
  if(message==WM_CREATE){v->dpi=GetDpiForWindow(window);v->Fonts();auto root=ModuleDir();v->saved.display=ReadAppearance(root);v->saved.schema=ReadSchemaPreference(root);v->saved.pinyin=v->saved.schema?v->saved.schema:ReadPinyinSchemaPreference(root);v->saved.double_schema=v->saved.pinyin>=2?v->saved.pinyin:ReadDoubleSchemaPreference(root);v->current=v->saved;
    RECT empty{0,0,1,1};for(auto item:{std::pair<int,const wchar_t*>{NavInput,L"输入方案"},{NavAppearance,L"外观"},{NavAbout,L"关于"}})v->Control(L"BUTTON",item.second,BS_OWNERDRAW|WS_TABSTOP,item.first,empty);
    v->pinyin_radio=v->Control(L"BUTTON",L"拼音",BS_AUTORADIOBUTTON|WS_GROUP|WS_TABSTOP,PinyinRadio,empty);v->wubi_radio=v->Control(L"BUTTON",L"五笔86",BS_AUTORADIOBUTTON,WubiRadio,empty);
    v->full_radio=v->Control(L"BUTTON",L"全拼",BS_AUTORADIOBUTTON|WS_GROUP|WS_TABSTOP,FullRadio,empty);v->double_radio=v->Control(L"BUTTON",L"双拼",BS_AUTORADIOBUTTON,DoubleRadio,empty);
    v->double_box=v->Control(L"COMBOBOX",L"双拼方案",CBS_DROPDOWNLIST|WS_TABSTOP,DoubleBox,empty);for(uint32_t i=2;i<ime::kSchemaCount;++i)SendMessageW(v->double_box,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(ime::kSchemaLabels[i]));
    v->font_box=v->Control(L"COMBOBOX",L"候选字号",CBS_DROPDOWNLIST|WS_TABSTOP|WS_VSCROLL,FontBox,empty);for(int i=12;i<=32;++i)SendMessageW(v->font_box,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(std::to_wstring(i).c_str()));
    v->theme_box=v->Control(L"COMBOBOX",L"候选配色",CBS_DROPDOWNLIST|WS_TABSTOP,ThemeBox,empty);for(auto text:{L"跟随系统",L"浅色",L"深色"})SendMessageW(v->theme_box,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(text));
    for(auto item:{std::pair<int,const wchar_t*>{ResetButton,L"恢复默认"},{HelpButton,L"使用说明"},{SaveButton,L"保存并应用"}}){auto control=v->Control(L"BUTTON",item.second,BS_OWNERDRAW|WS_TABSTOP,item.first,empty);if(item.first==SaveButton)v->save=control;}
    v->input_controls={v->pinyin_radio,v->wubi_radio,v->full_radio,v->double_radio,v->double_box};v->appearance_controls={v->font_box,v->theme_box};v->status_window=v->Control(L"STATIC",L"",SS_LEFT|SS_CENTERIMAGE,120,empty);v->Layout();return 0;}
  if(message==WM_ERASEBKGND)return 1;
  if(message==WM_PAINT){v->Paint();return 0;}
  if(message==WM_CTLCOLORSTATIC||message==WM_CTLCOLORBTN){auto dc=reinterpret_cast<HDC>(wp);SetBkMode(dc,TRANSPARENT);SetTextColor(dc,RGB(22,44,62));return reinterpret_cast<LRESULT>(reinterpret_cast<HWND>(lp)==v->status_window?v->background:v->card);}
  if(message==WM_DRAWITEM){auto& item=*reinterpret_cast<DRAWITEMSTRUCT*>(lp);wchar_t text[64]{};GetWindowTextW(item.hwndItem,text,64);auto p=Palette::Get(false);bool navigation=item.CtlID>=NavInput&&item.CtlID<=NavAbout;bool selected=navigation&&int(item.CtlID)-NavInput==v->page;
    auto fill=navigation?(selected?RGB(31,77,89):RGB(17,48,65)):item.CtlID==SaveButton?((item.itemState&ODS_DISABLED)?RGB(220,230,235):p.accent):p.background;
    Fill(item.hDC,item.rcItem,navigation?RGB(17,48,65):RGB(245,248,251));Rounded(item.hDC,item.rcItem,v->D(8),fill,navigation||item.CtlID==SaveButton?fill:p.border);Label(item.hDC,text,item.rcItem,v->strong,(item.itemState&ODS_DISABLED)?p.muted:navigation||item.CtlID==SaveButton?RGB(245,250,251):p.text,DT_CENTER);
    if(item.itemState&ODS_FOCUS){RECT r=item.rcItem;InflateRect(&r,-v->D(4),-v->D(4));DrawFocusRect(item.hDC,&r);}return TRUE;}
  if(message==WM_DPICHANGED){v->dpi=HIWORD(wp);auto r=reinterpret_cast<RECT*>(lp);SetWindowPos(window,nullptr,r->left,r->top,r->right-r->left,r->bottom-r->top,SWP_NOZORDER|SWP_NOACTIVATE);v->Fonts();v->Layout();return 0;}
  if(message==WM_COMMAND){int id=LOWORD(wp),event=HIWORD(wp);
    if(id>=NavInput&&id<=NavAbout){v->page=id-NavInput;v->Sync();return 0;}
    if(id==PinyinRadio){v->current.schema=v->current.pinyin;v->Changed();return 0;}if(id==WubiRadio){v->current.schema=0;v->Changed();return 0;}
    if(id==FullRadio){v->current.schema=v->current.pinyin=1;v->Changed();return 0;}if(id==DoubleRadio){v->current.schema=v->current.pinyin=v->current.double_schema;v->Changed();return 0;}
    if(id==DoubleBox&&event==CBN_SELCHANGE){v->current.schema=v->current.pinyin=v->current.double_schema=2+uint32_t(SendMessageW(v->double_box,CB_GETCURSEL,0,0));v->Changed();return 0;}
    if((id==FontBox||id==ThemeBox)&&event==CBN_SELCHANGE){v->current.display.font_size=12+int(SendMessageW(v->font_box,CB_GETCURSEL,0,0));v->current.display.theme=int(SendMessageW(v->theme_box,CB_GETCURSEL,0,0));v->Changed();return 0;}
    if(id==ResetButton){if(v->page==0){v->current.schema=v->current.pinyin=1;v->current.double_schema=2;}else if(v->page==1)v->current.display={};v->Changed();return 0;}
    if(id==SaveButton){bool apply=v->page==0||v->current.schema!=v->saved.schema||v->current.pinyin!=v->saved.pinyin||v->current.double_schema!=v->saved.double_schema;
      if(SavePreferences(ModuleDir()/L"settings.ini",v->current,apply)){v->saved=v->current;v->status=L"已保存；下次输入生效，未确认编码会取消。";}else v->status=L"保存失败，请检查此安装目录的写入权限。";v->Sync();return 0;}
    if(id==HelpButton){auto file=ModuleDir()/L"TESTING.txt";ShellExecuteW(window,L"open",file.c_str(),nullptr,nullptr,SW_SHOWNORMAL);return 0;}
  }
  if(message==WM_DESTROY){PostQuitMessage(0);return 0;}return DefWindowProcW(window,message,wp,lp);
}
}
int PreferencesUI(HINSTANCE instance,int show){
  auto initialized=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);if(FAILED(initialized))return 1;SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);INITCOMMONCONTROLSEX common{sizeof(common),ICC_STANDARD_CLASSES};InitCommonControlsEx(&common);
  View v;v.instance=instance;WNDCLASSW wc{};wc.hInstance=instance;wc.lpfnWndProc=Proc;wc.lpszClassName=L"YANVY.Settings.rc1";wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);wc.hIcon=LoadIconW(instance,MAKEINTRESOURCEW(1));RegisterClassW(&wc);
  UINT dpi=GetDpiForSystem();RECT rect{0,0,MulDiv(720,int(dpi),96),MulDiv(560,int(dpi),96)};DWORD style=WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX;AdjustWindowRectExForDpi(&rect,style,FALSE,0,dpi);
  auto window=CreateWindowExW(WS_EX_CONTROLPARENT,wc.lpszClassName,L"言舟 · 设置",style,CW_USEDEFAULT,CW_USEDEFAULT,rect.right-rect.left,rect.bottom-rect.top,nullptr,nullptr,instance,&v);if(!window){CoUninitialize();return 1;}ShowWindow(window,show);MSG message;while(GetMessageW(&message,nullptr,0,0)>0)if(!IsDialogMessageW(window,&message)){TranslateMessage(&message);DispatchMessageW(&message);}CoUninitialize();return 0;
}
#ifdef YANVY_PREFERENCES_FIXTURE
int PreferencesFixture(const std::filesystem::path& directory){
  if(std::filesystem::exists(directory)||!std::filesystem::create_directory(directory))return 1;
  const auto ini=directory/L"settings.ini";
  if(!WritePrivateProfileStringW(L"custom",L"synthetic",L"preserve-me",ini.c_str()))return 2;
  Preferences value;value.schema=value.pinyin=value.double_schema=4;value.display={32,2};
  if(!SavePreferences(ini,value)||ReadSchemaPreference(directory)!=4||ReadPinyinSchemaPreference(directory)!=4)return 3;
  auto display=ReadAppearance(directory);if(display.font_size!=32||display.theme!=2)return 4;
  if(ReadDoubleSchemaPreference(directory)!=4)return 4;
  if(!SavePreferences(ini,value,true))return 11;
  auto revision=ReadInputRevision(directory);
  if(revision.empty()||!SavePreferences(ini,value,true)||revision==ReadInputRevision(directory))return 12;
  revision=ReadInputRevision(directory);if(!SavePreferences(ini,value,false)||revision!=ReadInputRevision(directory))return 13;
  wchar_t custom[64]{};GetPrivateProfileStringW(L"custom",L"synthetic",L"",custom,64,ini.c_str());if(std::wstring(custom)!=L"preserve-me")return 5;
  for(auto file:std::filesystem::directory_iterator(directory))if(file.path().filename().wstring().find(L"pending-")!=std::wstring::npos)return 6;
  if(SavePreferences(directory/L"missing-parent"/L"settings.ini",value))return 7;
  if(ReadSchemaPreference(directory)!=4)return 8;
  WritePrivateProfileStringW(L"input",L"schema",L"99",ini.c_str());WritePrivateProfileStringW(L"input",L"pinyin_schema",L"999",ini.c_str());
  if(ReadSchemaPreference(directory)!=1||ReadPinyinSchemaPreference(directory)!=1)return 9;
  value.schema=0;value.pinyin=2;if(!SavePreferences(ini,value)||ReadSchemaPreference(directory)!=0||ReadPinyinSchemaPreference(directory)!=2)return 10;
  return 0;
}
#endif
}
