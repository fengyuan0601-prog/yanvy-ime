// SPDX-License-Identifier: BSD-3-Clause
// Visual surface only. The transaction, permissions and rollback stay in View.
  void Fonts(){ClearFonts();body=Font(D(14));caption_font=Font(D(12));headline=Font(D(27),FW_SEMIBOLD);brand=Font(D(32),FW_SEMIBOLD);strong=Font(D(14),FW_SEMIBOLD);
    icon=static_cast<HICON>(LoadImageW(instance,MAKEINTRESOURCEW(1),IMAGE_ICON,D(52),D(52),LR_SHARED));}
  const wchar_t* Title()const{return page==1?(uninstall?L"正在卸载言舟":L"正在为你准备言舟"):page==2?(uninstall?(configuration_failed?L"已卸载，清理需检查":L"已完成卸载"):configuration_failed?L"已安装，配置需检查":L"安装完成，准备输入"):page==3?L"还差一步，请检查":uninstall?L"卸载言舟":L"欢迎使用言舟";}
  const wchar_t* Primary()const{return page==1?L"正在处理…":page==2?L"完成":page==3?L"重新尝试":uninstall?L"确认卸载":L"开始安装";}
  void Text(HDC dc,const wchar_t* text,RECT rect,HFONT font,COLORREF color,bool wrap=false){
    if(!wrap){Label(dc,text,rect,font,color);return;}auto old=SelectObject(dc,font);SetBkMode(dc,TRANSPARENT);SetTextColor(dc,color);DrawTextW(dc,text,-1,&rect,DT_LEFT|DT_WORDBREAK|DT_NOPREFIX);SelectObject(dc,old);
  }
  void Button(HDC dc,RECT rect,const wchar_t* text,bool main,bool disabled=false,bool focus=false,bool pressed=false,bool hot=false){SurfaceButton(dc,rect,text,strong,D(8),Palette::Get(false),main,disabled,pressed,hot,focus);}
  void Draw(HDC dc,bool live){
    const auto p=Palette::Get(false);Fill(dc,R(0,0,800,560),Canvas());Fill(dc,R(0,0,224,560),RGB(235,244,244));Fill(dc,R(223,0,224,560),RGB(217,232,232));
    if(icon)DrawIconEx(dc,D(32),D(36),icon,D(52),D(52),0,nullptr,DI_NORMAL);
    Text(dc,L"言舟",R(32,109,194,157),brand,p.text);Text(dc,L"YANVY",R(34,162,194,188),strong,p.accent);
    Text(dc,L"把所想，\n写成文字。",R(32,224,202,306),headline,p.text,true);
    {Gdiplus::Graphics g(dc);g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);Gdiplus::Pen pen(Gdiplus::Color(90,81,155,149),float(D(1)));Gdiplus::GraphicsPath path;path.AddBezier(float(D(-40)),float(D(345)),float(D(60)),float(D(298)),float(D(98)),float(D(406)),float(D(224)),float(D(342)));g.DrawPath(&pen,&path);path.Reset();path.AddBezier(float(D(-40)),float(D(358)),float(D(72)),float(D(316)),float(D(100)),float(D(420)),float(D(224)),float(D(355)));g.DrawPath(&pen,&path);}
    Rounded(dc,R(28,410,196,438),D(14),RGB(220,238,232),RGB(220,238,232));Text(dc,L"●  离线 · 本机转换",R(42,412,190,435),caption_font,p.accent);
    Text(dc,L"免费开源，无广告",R(34,449,202,475),caption_font,p.muted);Text(dc,L"一念开物 · IDEORIN",R(32,512,210,541),caption_font,p.muted);
    Text(dc,uninstall?L"卸载向导":L"WINDOWS 输入法",R(256,28,520,51),caption_font,p.muted);Label(dc,kVersion,R(650,28,768,51),caption_font,p.muted,DT_RIGHT);
    if(!live)Text(dc,Title(),R(256,68,768,111),headline,p.text);
    const wchar_t* subtitle=page==1?L"请保持窗口打开，完成后即可开始使用。":page==2?(uninstall?L"感谢使用。你的其他输入方式不受影响。":configuration_failed?L"请查看下方提示，完成当前用户的配置。":L"从系统输入法列表选择言舟，开启新的输入体验。"):
      page==3?L"按下方提示调整后重试，你也可以查看本机诊断。":uninstall?L"移除言舟的系统组件，保留你的个人设置。":L"全拼、双拼、五笔，一套熟悉的输入习惯。";
    Text(dc,subtitle,R(256,122,768,160),body,p.muted,true);
    if(page==0||page==3){
      Rounded(dc,R(256,176,768,329),D(12),p.background,p.border);Text(dc,uninstall?L"系统组件位置":L"词库位置",R(274,190,480,215),strong,p.text);
      if(!uninstall){Label(dc,L"可直接输入完整路径",R(490,190,750,215),caption_font,p.muted,DT_RIGHT);Rounded(dc,R(270,222,672,266),D(6),p.background,p.border);}
      if(!live)Text(dc,target.c_str(),R(274,228,uninstall?746:664,261),body,p.text);
      if(!live&&!uninstall)Button(dc,R(682,225,750,263),L"更改",false);
      Fill(dc,R(274,277,750,278),p.line);Text(dc,uninstall?L"只移除已列明的程序文件，保留个人设置。":L"可自由选择本地文件夹。系统组件由 Windows 安全管理。",R(274,289,750,315),caption_font,p.muted);
      Text(dc,uninstall?L"保留其他输入法与个人设置":L"默认输入方式",R(256,352,464,385),strong,p.text);
      if(!live&&!uninstall){Rounded(dc,R(476,351,768,389),D(6),p.background,p.border);Text(dc,ime::kSchemaLabels[schema],R(488,353,738,386),body,p.text);Text(dc,L"⌄",R(741,353,762,386),body,p.muted);}
      Text(dc,uninstall?L"被应用占用或不在清单内的文件会安全保留。":L"✓ 保留其他输入法与设置，之后可随时更改方案。",R(256,404,768,431),caption_font,p.muted);
    }else if(page==1){
      Rounded(dc,R(256,176,768,422),D(12),p.background,p.border);
      Text(dc,uninstall?L"正在安全移除组件":L"安装只需片刻",R(276,193,748,226),strong,p.text);
      const wchar_t* steps[]={uninstall?L"注销系统输入法":L"准备离线词库",uninstall?L"移除已列明的程序":L"安装并注册系统组件",uninstall?L"保留个人设置":L"启用输入法与所选方案"};
      int active=stage==Stage::prepare_data?0:stage==Stage::machine?1:2;
      for(int i=0;i<3;++i){int y=239+i*42;RECT badge=R(276,y+3,298,y+25);Rounded(dc,badge,D(11),i<=active?p.selection:p.chip,i<=active?p.selection:p.chip);auto number=std::to_wstring(i+1);Label(dc,number.c_str(),badge,caption_font,i<=active?p.accent:p.muted,DT_CENTER);Text(dc,steps[i],R(310,y,746,y+30),body,i==active?p.text:p.muted);}
      if(!live){Fill(dc,R(276,385,748,391),p.line);Rounded(dc,R(276,385,440,391),D(3),p.accent,p.accent);}
    }else if(page==2){
      Rounded(dc,R(256,176,768,424),D(12),p.background,p.border);
      Rounded(dc,R(276,196,308,228),D(16),p.selection,p.selection);Label(dc,L"✓",R(276,196,308,228),strong,p.accent,DT_CENTER);Text(dc,uninstall?L"已移除输入法注册":L"接下来，这样使用",R(322,194,746,231),strong,p.text);
      const wchar_t* steps[]={L"在系统输入法列表中选择“言舟”",L"按 Ctrl + 空格，在中文与英文间切换",L"右键“中/英”按钮，随时打开设置"};
      if(uninstall)Text(dc,L"其他输入法与个人设置不受影响。\n被应用占用的文件会保留至能够安全清理。",R(276,253,746,376),body,p.muted,true);
      else for(int i=0;i<3;++i){int y=252+i*48;auto number=std::to_wstring(i+1);Label(dc,number.c_str(),R(278,y,302,y+28),caption_font,p.accent);Text(dc,steps[i],R(318,y,748,y+30),body,p.text);}
    }
    if(!live&&!status.empty())Text(dc,status.c_str(),R(256,447,768,484),caption_font,page==3?RGB(165,58,44):p.muted,true);
    Fill(dc,R(256,496,768,497),p.line);
    if(!live){Text(dc,NeedsDiagnostic()?L"查看诊断":L"版本与许可",R(256,520,438,546),caption_font,p.accent);if(page!=1&&page!=2)Button(dc,R(522,514,614,552),L"取消",false);
      if(page==2&&!uninstall)Button(dc,R(506,514,614,552),L"打开设置",false);
      Button(dc,R(630,514,768,552),Primary(),true,page==1);}
  }
  void Layout(){
    auto place=[&](HWND control,RECT r){SetWindowPos(control,nullptr,r.left,r.top,r.right-r.left,r.bottom-r.top,SWP_NOZORDER|SWP_NOACTIVATE);};
    place(heading,R(256,68,768,111));SendMessageW(heading,WM_SETFONT,reinterpret_cast<WPARAM>(headline),TRUE);
    place(path_control,R(274,228,uninstall?746:664,261));SendMessageW(path_control,WM_SETFONT,reinterpret_cast<WPARAM>(body),TRUE);
    place(status_control,R(256,447,768,484));SendMessageW(status_control,WM_SETFONT,reinterpret_cast<WPARAM>(caption_font),TRUE);
    place(primary,R(630,514,768,552));place(secondary,R(522,514,614,552));place(browse,R(682,225,750,263));place(schema_box,R(476,351,768,520));
    SendMessageW(schema_box,WM_SETFONT,reinterpret_cast<WPARAM>(body),TRUE);SendMessageW(schema_box,CB_SETITEMHEIGHT,WPARAM(-1),D(32));
    place(notes,R(256,514,438,552));place(settings,R(506,514,614,552));place(progress,R(276,385,748,393));
    for(auto control:{primary,secondary,browse,settings,notes}){SendMessageW(control,WM_SETFONT,reinterpret_cast<WPARAM>(strong),TRUE);TrackSurface(control);}Refresh();
  }
  void Refresh(){
    StyleCombo(schema_box,false,dpi);
    SetWindowTextW(notes,NeedsDiagnostic()?L"查看诊断":L"版本与许可");SetWindowTextW(heading,Title());SetWindowTextW(primary,Primary());SetWindowTextW(status_control,status.c_str());SetWindowTextW(path_control,target.c_str());
    EnableWindow(primary,page!=1);ShowWindow(secondary,page!=1&&page!=2?SW_SHOW:SW_HIDE);ShowWindow(settings,page==2&&!uninstall?SW_SHOW:SW_HIDE);
    ShowWindow(browse,(page==0||page==3)&&!uninstall?SW_SHOW:SW_HIDE);ShowWindow(path_control,page==0||page==3?SW_SHOW:SW_HIDE);
    ShowWindow(schema_box,(page==0||page==3)&&!uninstall?SW_SHOW:SW_HIDE);SendMessageW(schema_box,CB_SETCURSEL,schema,0);
    ShowWindow(progress,page==1?SW_SHOW:SW_HIDE);SendMessageW(progress,PBM_SETMARQUEE,page==1,35);EnableWindow(notes,page!=1);InvalidateRect(window,nullptr,FALSE);
  }
