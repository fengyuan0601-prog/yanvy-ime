// SPDX-License-Identifier: BSD-3-Clause
// Included in preferences_ui.cpp's private namespace; persistence stays there.
enum {SchemeCard=210};
struct View {
  HINSTANCE instance{};HWND window{},status_window{},font_box{},theme_box{},save{};UINT dpi=96;int page=0;
  HFONT body{},title{},caption{},strong{},brand{};HICON icon{};
  HBRUSH background=nullptr,card=nullptr;
  Preferences saved,current;CandidateView preview;
  std::filesystem::path preview_root;
  std::wstring status=L"选择一种输入方式，保存后即可使用。";
  std::vector<HWND> scheme_controls,appearance_controls;
  ~View(){ClearFonts();if(background)DeleteObject(background);if(card)DeleteObject(card);}
  bool Dark()const{return IsDark(current.display);}
  int D(int n)const{return MulDiv(n,int(dpi),96);}RECT R(int l,int t,int r,int b)const{return {D(l),D(t),D(r),D(b)};}
  void ClearFonts(){for(auto font:{body,title,caption,strong,brand})if(font)DeleteObject(font);}
  void Fonts(){ClearFonts();body=Font(D(14));title=Font(D(28),FW_SEMIBOLD);caption=Font(D(12));strong=Font(D(15),FW_SEMIBOLD);brand=Font(D(20),FW_SEMIBOLD);
    icon=static_cast<HICON>(LoadImageW(instance,MAKEINTRESOURCEW(1),IMAGE_ICON,D(36),D(36),LR_SHARED));}
  HWND Control(const wchar_t* cls,const wchar_t* text,DWORD style,int id){
    auto control=CreateWindowExW(0,cls,text,WS_CHILD|WS_VISIBLE|style,0,0,1,1,window,reinterpret_cast<HMENU>(INT_PTR(id)),instance,nullptr);
    SendMessageW(control,WM_SETFONT,reinterpret_cast<WPARAM>(body),TRUE);TrackSurface(control);return control;
  }
  void Text(HDC dc,const wchar_t* text,RECT rect,HFONT font,COLORREF color,bool wrap=false){
    if(!wrap){Label(dc,text,rect,font,color);return;}auto old=SelectObject(dc,font);SetBkMode(dc,TRANSPARENT);SetTextColor(dc,color);DrawTextW(dc,text,-1,&rect,DT_LEFT|DT_WORDBREAK|DT_NOPREFIX);SelectObject(dc,old);
  }
  void UpdatePreview(){Response sample;sample.count=1;sample.last_page=1;wcscpy_s(sample.input,ime::kSchemaExamples[current.schema]);wcscpy_s(sample.preedit,ime::kSchemaExamples[current.schema]);wcscpy_s(sample.candidates[0],L"你好");preview.Set(sample,current.schema,false,current.display,dpi,icon);}
  void Sync(){
    BOOL dark=Dark();DwmSetWindowAttribute(window,20,&dark,sizeof(dark));
    StyleCombo(font_box,dark,dpi);StyleCombo(theme_box,dark,dpi);
    const auto p=Palette::Get(Dark());if(background)DeleteObject(background);if(card)DeleteObject(card);background=CreateSolidBrush(Canvas(Dark()));card=CreateSolidBrush(p.background);
    for(uint32_t i=0;i<scheme_controls.size();++i){auto name=std::wstring(ime::kSchemaLabels[i])+(current.schema==i?L" · 已选择":L" · 选择此方案");SetWindowTextW(scheme_controls[i],name.c_str());ShowWindow(scheme_controls[i],page==0?SW_SHOW:SW_HIDE);}
    for(auto control:appearance_controls)ShowWindow(control,page==1?SW_SHOW:SW_HIDE);
    SendMessageW(font_box,CB_SETCURSEL,current.display.font_size-12,0);SendMessageW(theme_box,CB_SETCURSEL,current.display.theme,0);
    EnableWindow(save,page==0||!current.Equals(saved));ShowWindow(save,page==2?SW_HIDE:SW_SHOW);ShowWindow(GetDlgItem(window,ResetButton),page==2?SW_HIDE:SW_SHOW);
    SetWindowTextW(status_window,status.c_str());UpdatePreview();InvalidateRect(window,nullptr,FALSE);
    for(auto id:{NavInput,NavAppearance,NavAbout,SaveButton,ResetButton,HelpButton})InvalidateRect(GetDlgItem(window,id),nullptr,FALSE);
  }
  void Choose(uint32_t schema){current.schema=schema;if(schema){current.pinyin=schema;if(schema>=2)current.double_schema=schema;}status=std::wstring(L"已选择")+ime::kSchemaLabels[schema]+L"，保存后下次输入生效。";Sync();}
  void Changed(){status=L"预览已更新，保存后生效。";Sync();}
  void Layout(){
    auto place=[&](int id,RECT r){auto c=GetDlgItem(window,id);SetWindowPos(c,nullptr,r.left,r.top,r.right-r.left,r.bottom-r.top,SWP_NOZORDER|SWP_NOACTIVATE);SendMessageW(c,WM_SETFONT,reinterpret_cast<WPARAM>(body),TRUE);};
    place(NavInput,R(16,120,160,164));place(NavAppearance,R(16,176,160,220));place(NavAbout,R(16,232,160,276));
    place(SchemeCard+1,R(212,140,514,242));place(SchemeCard,R(530,140,832,242));
    place(SchemeCard+2,R(212,290,408,390));place(SchemeCard+3,R(424,290,620,390));place(SchemeCard+4,R(636,290,832,390));
    place(FontBox,R(660,126,812,300));place(ThemeBox,R(636,208,812,380));
    SendMessageW(font_box,CB_SETITEMHEIGHT,WPARAM(-1),D(32));SendMessageW(theme_box,CB_SETITEMHEIGHT,WPARAM(-1),D(32));
    place(ResetButton,R(212,544,316,582));place(HelpButton,R(328,544,432,582));place(SaveButton,R(688,544,832,582));
    place(120,R(212,594,832,616));SendMessageW(status_window,WM_SETFONT,reinterpret_cast<WPARAM>(caption),TRUE);Sync();
  }
  void Paint(){
    PAINTSTRUCT paint{};HDC dc=BeginPaint(window,&paint);RECT bounds{};GetClientRect(window,&bounds);HDC buffer=CreateCompatibleDC(dc);auto bitmap=CreateCompatibleBitmap(dc,bounds.right,bounds.bottom);auto old=SelectObject(buffer,bitmap);const auto p=Palette::Get(Dark());
    Fill(buffer,bounds,Canvas(Dark()));Fill(buffer,R(0,0,176,620),p.background);Fill(buffer,R(175,0,176,620),p.line);
    if(icon)DrawIconEx(buffer,D(22),D(29),icon,D(36),D(36),0,nullptr,DI_NORMAL);
    Text(buffer,L"言舟",R(68,25,156,55),brand,p.text);Text(buffer,L"YANVY",R(69,56,156,77),caption,p.muted);
    Rounded(buffer,R(20,480,156,551),D(10),p.chip,p.chip);Text(buffer,L"离线 · 本机转换",R(32,491,150,516),caption,p.text);Text(buffer,L"默认不学习",R(32,518,148,541),caption,p.muted);
    Text(buffer,L"一念开物 · IDEORIN",R(22,579,167,606),caption,p.muted);
    const wchar_t* heading=page==0?L"找到顺手的输入方式":page==1?L"看得清楚，用得舒服":L"让表达，有自己的空间。";
    Text(buffer,heading,R(212,26,770,66),title,p.text);Label(buffer,kVersion,R(775,30,832,58),caption,p.muted,DT_RIGHT);
    Text(buffer,page==0?L"全拼、双拼与五笔，一眼找到你的习惯。":page==1?L"调整候选文字与配色，效果在下方实时预览。":L"言舟 · YANVY   /   免费开源的本地输入法",R(212,73,832,98),body,p.muted);
    if(page==0){
      Text(buffer,L"常用输入",R(212,108,832,132),caption,p.muted);Text(buffer,L"双拼方案",R(212,258,500,282),caption,p.muted);Label(buffer,L"熟悉双拼键位时选择",R(500,258,832,282),caption,p.muted,DT_RIGHT);
      Rounded(buffer,R(212,418,832,506),D(12),p.background,p.border);
      Text(buffer,L"中文 / English",R(230,430,490,458),strong,p.text);Rounded(buffer,R(648,433,812,461),D(6),p.chip,p.chip);Label(buffer,L"Ctrl + 空格",R(648,433,812,461),caption,p.accent,DT_CENTER);
      Text(buffer,L"左键“中/英”切换语言，右键打开设置。",R(230,467,812,490),caption,p.muted);
    }else if(page==1){
      Rounded(buffer,R(212,112,832,264),D(12),p.background,p.border);Text(buffer,L"候选字号",R(232,126,620,156),strong,p.text);Text(buffer,L"12–32，按你的阅读习惯调整",R(232,165,620,187),caption,p.muted);
      Fill(buffer,R(232,194,812,195),p.line);Text(buffer,L"候选配色",R(232,211,610,240),strong,p.text);
      Text(buffer,L"实时预览",R(212,280,600,305),caption,p.muted);Label(buffer,L"示例文字 · 不产生真实输入",R(510,280,832,305),caption,p.muted,DT_RIGHT);
      RECT area=R(212,316,832,518);Rounded(buffer,area,D(12),p.chip,p.line);auto size=preview.Measure(buffer,D(580),D(180));int x=(area.left+area.right-size.cx)/2,y=(area.top+area.bottom-size.cy)/2;preview.Draw(buffer,{x,y,x+size.cx,y+size.cy});
    }else{
      Rounded(buffer,R(212,120,832,286),D(14),p.background,p.border);
      if(icon)DrawIconEx(buffer,D(236),D(144),icon,D(52),D(52),0,nullptr,DI_NORMAL);
      Text(buffer,L"言舟 · YANVY",R(310,140,802,176),brand,p.text);Text(buffer,L"让文字顺畅落下，让表达轻松一点。",R(310,182,802,209),body,p.muted);
      Text(buffer,L"Windows 11 x64 · ",R(236,235,403,262),caption,p.muted);Text(buffer,kVersion,R(403,235,490,262),caption,p.accent);Text(buffer,L"安装包未签名",R(530,235,802,262),caption,p.muted);
      const wchar_t* labels[]={L"本机转换",L"默认不学习",L"免费开源"};const wchar_t* notes[]={L"输入与候选在本地处理",L"不保存正文与剪贴板历史",L"无广告，无需登录"};
      for(int i=0;i<3;++i){int y=309+i*58;Text(buffer,labels[i],R(216,y,355,y+29),strong,p.text);Text(buffer,notes[i],R(360,y,832,y+29),body,p.muted);if(i<2)Fill(buffer,R(212,y+43,832,y+44),p.line);}
      Text(buffer,L"原创核心与文档：BSD-3-Clause。Windows 衍生前端及双拼方案保留 GPL 许可。\n完整来源、许可证和适用范围随安装包提供。",R(212,493,832,535),caption,p.muted,true);
    }
    if(page!=2)Fill(buffer,R(212,530,832,531),p.line);
    BitBlt(dc,0,0,bounds.right,bounds.bottom,buffer,0,0,SRCCOPY);SelectObject(buffer,old);DeleteObject(bitmap);DeleteDC(buffer);EndPaint(window,&paint);
  }
  void DrawControl(DRAWITEMSTRUCT& item){
    const auto p=Palette::Get(Dark());const bool focus=item.itemState&ODS_FOCUS,disabled=item.itemState&ODS_DISABLED,pressed=item.itemState&ODS_SELECTED,hot=SurfaceHot(item.hwndItem);
    if(item.CtlID>=SchemeCard&&item.CtlID<SchemeCard+ime::kSchemaCount){
      const uint32_t schema=item.CtlID-SchemeCard;const bool selected=schema==current.schema;auto rect=item.rcItem;
      Fill(item.hDC,rect,Canvas(Dark()));Rounded(item.hDC,rect,D(11),selected?p.selection:hot?p.hover:p.background,selected||focus?p.accent:p.border);
      const wchar_t* descriptions[]={L"按字形编码，纯五笔86",L"完整拼音，轻松上手",L"小鹤键位",L"自然码键位",L"微软键位"};
      RECT name{D(16),D(12),rect.right-D(32),D(40)};Label(item.hDC,ime::kSchemaLabels[schema],name,strong,selected?p.selection_text:p.text);
      RECT detail{D(16),D(42),rect.right-D(12),D(64)};Label(item.hDC,descriptions[schema],detail,caption,p.muted);
      const auto example=std::wstring(ime::kSchemaExamples[schema])+L"  →  你好";Label(item.hDC,example.c_str(),{D(16),D(68),rect.right-D(12),D(91)},caption,selected?p.accent:p.muted);
      RECT mark{rect.right-D(31),D(16),rect.right-D(13),D(34)};Rounded(item.hDC,mark,D(9),selected?p.accent:p.background,selected?p.accent:p.border);if(selected)Label(item.hDC,L"✓",mark,caption,p.badge_text,DT_CENTER);
      if(focus){InflateRect(&rect,-D(4),-D(4));FocusOutline(item.hDC,rect,D(8),p.accent);}return;
    }
    if(item.CtlID==FontBox||item.CtlID==ThemeBox){if(item.itemID==UINT(-1))return;Fill(item.hDC,item.rcItem,(item.itemState&ODS_SELECTED)?p.selection:p.background);wchar_t text[80]{};SendMessageW(item.hwndItem,CB_GETLBTEXT,item.itemID,reinterpret_cast<LPARAM>(text));auto r=item.rcItem;r.left+=D(10);Label(item.hDC,text,r,body,(item.itemState&ODS_DISABLED)?p.muted:p.text);return;}
    wchar_t text[80]{};GetWindowTextW(item.hwndItem,text,80);const bool nav=item.CtlID>=NavInput&&item.CtlID<=NavAbout;
    if(nav){const bool selected=int(item.CtlID)-NavInput==page;Fill(item.hDC,item.rcItem,p.background);Rounded(item.hDC,item.rcItem,D(8),selected?p.selection:hot?p.hover:p.background,selected?p.selection:p.background);auto r=item.rcItem;r.left+=D(18);Label(item.hDC,text,r,strong,selected?p.accent:p.muted);if(selected)Fill(item.hDC,{D(4),D(12),D(7),D(32)},p.accent);if(focus){r=item.rcItem;InflateRect(&r,-D(3),-D(3));FocusOutline(item.hDC,r,D(6),p.accent);}return;}
    Fill(item.hDC,item.rcItem,Canvas(Dark()));SurfaceButton(item.hDC,item.rcItem,text,strong,D(8),p,item.CtlID==SaveButton,disabled,pressed,hot,focus);
  }
};
LRESULT CALLBACK Proc(HWND window,UINT message,WPARAM wp,LPARAM lp){
  auto v=reinterpret_cast<View*>(GetWindowLongPtrW(window,GWLP_USERDATA));if(message==WM_NCCREATE){v=static_cast<View*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);v->window=window;SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(v));}if(!v)return DefWindowProcW(window,message,wp,lp);
  if(message==WM_CREATE){v->dpi=FitSurfaceDpi(window,GetDpiForWindow(window),860,620);v->Fonts();auto root=v->preview_root.empty()?PreferenceRoot(ModuleDir()):v->preview_root;v->saved.display=ReadAppearance(root);v->saved.schema=ReadSchemaPreference(root);v->saved.pinyin=v->saved.schema?v->saved.schema:ReadPinyinSchemaPreference(root);v->saved.double_schema=v->saved.pinyin>=2?v->saved.pinyin:ReadDoubleSchemaPreference(root);v->current=v->saved;
    for(auto item:{std::pair<int,const wchar_t*>{NavInput,L"输入方式"},{NavAppearance,L"外观与显示"},{NavAbout,L"关于言舟"}})v->Control(L"BUTTON",item.second,BS_OWNERDRAW|WS_TABSTOP,item.first);
    v->scheme_controls.resize(ime::kSchemaCount);for(uint32_t i:{1u,0u,2u,3u,4u})v->scheme_controls[i]=v->Control(L"BUTTON",ime::kSchemaLabels[i],BS_OWNERDRAW|WS_TABSTOP,SchemeCard+int(i));
    v->font_box=v->Control(L"COMBOBOX",L"候选字号",CBS_DROPDOWNLIST|CBS_OWNERDRAWFIXED|CBS_HASSTRINGS|WS_TABSTOP|WS_VSCROLL,FontBox);for(int i=12;i<=32;++i){auto text=std::to_wstring(i)+L"  号";SendMessageW(v->font_box,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(text.c_str()));}
    v->theme_box=v->Control(L"COMBOBOX",L"候选配色",CBS_DROPDOWNLIST|CBS_OWNERDRAWFIXED|CBS_HASSTRINGS|WS_TABSTOP,ThemeBox);for(auto text:{L"跟随系统",L"浅色 · 云白",L"深色 · 夜航"})SendMessageW(v->theme_box,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(text));
    for(auto item:{std::pair<int,const wchar_t*>{ResetButton,L"恢复默认"},{HelpButton,L"使用说明"},{SaveButton,L"保存并应用"}}){auto control=v->Control(L"BUTTON",item.second,BS_OWNERDRAW|WS_TABSTOP,item.first);if(item.first==SaveButton)v->save=control;}
    v->appearance_controls={v->font_box,v->theme_box};v->status_window=v->Control(L"STATIC",L"",SS_LEFT|SS_CENTERIMAGE,120);v->Layout();return 0;}
  if(message==WM_ERASEBKGND)return 1;
  if(message==WM_PAINT){v->Paint();return 0;}
  if(message==WM_CTLCOLORSTATIC||message==WM_CTLCOLORBTN||message==WM_CTLCOLORLISTBOX){const auto p=Palette::Get(v->Dark());auto dc=reinterpret_cast<HDC>(wp);SetBkMode(dc,TRANSPARENT);SetTextColor(dc,p.text);SetBkColor(dc,p.background);return reinterpret_cast<LRESULT>(reinterpret_cast<HWND>(lp)==v->status_window?v->background:v->card);}
  if(message==WM_MEASUREITEM){auto item=reinterpret_cast<MEASUREITEMSTRUCT*>(lp);item->itemHeight=v->D(30);return TRUE;}
  if(message==WM_DRAWITEM){v->DrawControl(*reinterpret_cast<DRAWITEMSTRUCT*>(lp));return TRUE;}
  if(message==WM_DPICHANGED){v->dpi=FitSurfaceDpi(window,HIWORD(wp),860,620);RECT r{0,0,v->D(860),v->D(620)};AdjustWindowRectExForDpi(&r,GetWindowLongW(window,GWL_STYLE),FALSE,0,HIWORD(wp));auto suggested=reinterpret_cast<RECT*>(lp);SetWindowPos(window,nullptr,suggested->left,suggested->top,r.right-r.left,r.bottom-r.top,SWP_NOZORDER|SWP_NOACTIVATE);v->Fonts();v->Layout();return 0;}
  if(message==WM_SETTINGCHANGE){v->Sync();return 0;}
  if(message==WM_COMMAND){int id=LOWORD(wp),event=HIWORD(wp);
    if(id>=NavInput&&id<=NavAbout){v->page=id-NavInput;v->Sync();return 0;}
    if(id>=SchemeCard&&id<SchemeCard+int(ime::kSchemaCount)){v->Choose(uint32_t(id-SchemeCard));return 0;}
    if((id==FontBox||id==ThemeBox)&&event==CBN_SELCHANGE){v->current.display.font_size=12+int(SendMessageW(v->font_box,CB_GETCURSEL,0,0));v->current.display.theme=int(SendMessageW(v->theme_box,CB_GETCURSEL,0,0));v->Changed();return 0;}
    if(id==ResetButton){if(v->page==0){v->current.schema=v->current.pinyin=1;v->current.double_schema=2;}else if(v->page==1)v->current.display={};v->Changed();return 0;}
    if(id==SaveButton){bool apply=v->page==0||v->current.schema!=v->saved.schema||v->current.pinyin!=v->saved.pinyin||v->current.double_schema!=v->saved.double_schema;auto user=v->preview_root.empty()?UserDataRoot():v->preview_root;
      if(EnsureUserSettings(ModuleDir(),user)&&SavePreferences(user/L"settings.ini",v->current,apply)){v->saved=v->current;v->status=L"✓ 已保存。下次输入生效，当前未确认的编码会取消。";}else v->status=L"保存失败，请检查设置目录的写入权限。";v->Sync();return 0;}
    if(id==HelpButton){auto file=ModuleDir()/L"TESTING.txt";ShellExecuteW(window,L"open",file.c_str(),nullptr,nullptr,SW_SHOWNORMAL);return 0;}
  }
  if(message==WM_DESTROY){PostQuitMessage(0);return 0;}return DefWindowProcW(window,message,wp,lp);
}
