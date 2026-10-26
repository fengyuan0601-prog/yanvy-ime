; SPDX-License-Identifier: GPL-3.0-or-later
Unicode true
!include "LogicLib.nsh"
!include "x64.nsh"
!ifndef STAGE
  !error "Pass /DSTAGE=absolute-clean-staging-directory"
!endif
!ifndef OUTPUT
  !error "Pass /DOUTPUT=absolute-installer-path"
!endif
Name "言舟 · YANVY 0.1.0-dev.1"
OutFile "${OUTPUT}"
InstallDir "E:\Codex\Programs\YANVY\0.1.0-dev.1"
; The public native bootstrap requests UAC, sets E: temp, then launches this
; internal payload with its elevated token and inherited environment.
RequestExecutionLevel user
SetCompressor /SOLID lzma
SetDatablockOptimize on
BrandingText "IDEORIN · 开发测试版 / 未签名"
Icon "..\..\..\assets\brand\yanvy.ico"
UninstallIcon "..\..\..\assets\brand\yanvy.ico"
ShowInstDetails show
ShowUninstDetails show
VIProductVersion "0.1.0.1"
VIAddVersionKey /LANG=1033 "ProductName" "YANVY"
VIAddVersionKey /LANG=1033 "FileDescription" "YANVY Windows x64 developer test installer"
VIAddVersionKey /LANG=1033 "FileVersion" "0.1.0-dev.1"
VIAddVersionKey /LANG=1033 "LegalCopyright" "YANVY contributors; upstream licenses preserved"
LicenseData "${STAGE}\TESTING.txt"
LicenseText "请先阅读本测试包的范围、操作和卸载说明。继续安装即表示你已了解这是开发测试版。"
Page license
Page directory
Page instfiles
UninstPage uninstConfirm
UninstPage instfiles
Var PreviousDir

Function .onInit
  ReadEnvStr $0 "YANVY_SETUP_TEMP_READY"
  ${If} $0 != "1"
    MessageBox MB_OK|MB_ICONSTOP "请运行对外提供的 YANVY 安装包入口。此文件是内部安装载荷。"
    Abort
  ${EndIf}
  ${IfNot} ${IsNativeAMD64}
    MessageBox MB_OK|MB_ICONSTOP "此测试包只支持 Windows x64，不支持 32 位或 ARM64 Windows。"
    Abort
  ${EndIf}
  SetRegView 64
  SetShellVarContext all
  ReadRegStr $PreviousDir HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\YANVY-dev" "InstallLocation"
FunctionEnd

Section "言舟 Windows TSF 开发测试"
  SetRegView 64
  SetShellVarContext all
  ; Never replace an arbitrary directory that merely happens to share a name.
  IfFileExists "$INSTDIR\YANVY-INSTALL-MARKER.txt" existing_owned
  IfFileExists "$INSTDIR\*.*" 0 install_files
    MessageBox MB_OK|MB_ICONSTOP "请选择新的空目录。安装程序不会覆盖已有目录中的文件。"
    Abort
  existing_owned:
    MessageBox MB_OK|MB_ICONINFORMATION "这个开发版本已经安装。请先卸载，或选择新的空目录；旧配置不会被静默覆盖。"
    Abort
  install_files:
  SetOutPath "$INSTDIR"
  File /r "${STAGE}\*.*"
  ; Warm the fixed data using synthetic input before system registration.
  ExecWait '"$INSTDIR\YANVY-Server.exe" --check' $0
  ${If} $0 != 0
    MessageBox MB_OK|MB_ICONSTOP "固定词库或隐私策略校验失败，未启用言舟。请保留此错误类别并报告。"
    Goto rollback
  ${EndIf}
  ExecWait '"$INSTDIR\YANVY-Setup.exe" --register --machine' $0
  ${If} $0 != 0
    MessageBox MB_OK|MB_ICONSTOP "Windows TSF 注册失败。请确认已允许管理员授权；安装程序将回退此次注册。"
    Goto rollback
  ${EndIf}
  WriteUninstaller "$INSTDIR\Uninstall-core.exe"
  CreateDirectory "$SMPROGRAMS\言舟 YANVY 开发测试"
  CreateShortcut "$SMPROGRAMS\言舟 YANVY 开发测试\言舟设置.lnk" "$INSTDIR\YANVY-Settings.exe"
  CreateShortcut "$SMPROGRAMS\言舟 YANVY 开发测试\启动言舟引擎.lnk" "$INSTDIR\YANVY-Server.exe" "--serve"
  CreateShortcut "$SMPROGRAMS\言舟 YANVY 开发测试\测试说明.lnk" "$INSTDIR\TESTING.txt"
  CreateShortcut "$SMPROGRAMS\言舟 YANVY 开发测试\卸载.lnk" "$INSTDIR\Uninstall.exe"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\YANVY-dev" "DisplayName" "言舟 · YANVY 0.1.0-dev.1 (开发测试)"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\YANVY-dev" "DisplayVersion" "0.1.0-dev.1"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\YANVY-dev" "Publisher" "IDEORIN / fengyuan0601-prog"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\YANVY-dev" "InstallLocation" "$INSTDIR"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\YANVY-dev" "UninstallString" '$\"$INSTDIR\Uninstall.exe$\"'
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\YANVY-dev" "DisplayIcon" "$INSTDIR\YANVY-Settings.exe"
  WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\YANVY-dev" "NoModify" 1
  WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\YANVY-dev" "NoRepair" 1
  DetailPrint "安装完成。请从开始菜单启动言舟引擎，再在正常应用里选择言舟输入法。"
  DetailPrint "未更改系统默认输入法。测试前请重新打开目标应用。"
  Goto done
  rollback:
    ExecWait '"$INSTDIR\YANVY-Setup.exe" --unregister --machine' $0
    ${If} $PreviousDir != ""
      IfFileExists "$PreviousDir\YANVY-Setup.exe" 0 +2
        ExecWait '"$PreviousDir\YANVY-Setup.exe" --register --machine' $0
    ${EndIf}
    !include "${STAGE}-uninstall.nsh"
    RMDir "$INSTDIR"
    SetErrorLevel 1
    Abort
  done:
SectionEnd

Function un.onInit
  ReadEnvStr $0 "YANVY_SETUP_TEMP_READY"
  ${If} $0 != "1"
    MessageBox MB_OK|MB_ICONSTOP "请运行安装目录的 Uninstall.exe 卸载入口。"
    Abort
  ${EndIf}
  SetRegView 64
  SetShellVarContext all
  IfFileExists "$INSTDIR\YANVY-INSTALL-MARKER.txt" +3
    MessageBox MB_OK|MB_ICONSTOP "未找到言舟安装标记，停止卸载以保护其他目录。"
    Abort
FunctionEnd

Section "Uninstall"
  SetRegView 64
  SetShellVarContext all
  ; Unregister first. Do not remove files if Windows rejects the unregister.
  ExecWait '"$INSTDIR\YANVY-Setup.exe" --unregister --machine' $0
  ${If} $0 != 0
    MessageBox MB_OK|MB_ICONSTOP "Windows 未完成输入法注销，文件已保留。请先切换输入法、关闭使用言舟的应用后重试。"
    Abort
  ${EndIf}
  ExecWait '"$INSTDIR\YANVY-Server.exe" --stop' $0
  Sleep 600
  !include "${STAGE}-uninstall.nsh"
  ; Remove only engine-generated fixed cache names; preserve unknown files
  ; and any manually-created settings.ini in the installation directory.
  Delete "$INSTDIR\profile\user.yaml"
  Delete "$INSTDIR\profile\installation.yaml"
  Delete "$INSTDIR\profile\build\default.yaml"
  Delete "$INSTDIR\profile\build\wubi86.schema.yaml"
  Delete "$INSTDIR\profile\build\pinyin_simp.schema.yaml"
  Delete "$INSTDIR\profile\build\stroke.schema.yaml"
  Delete "$INSTDIR\profile\build\wubi86.table.bin"
  Delete "$INSTDIR\profile\build\wubi86.prism.bin"
  Delete "$INSTDIR\profile\build\wubi86.reverse.bin"
  Delete "$INSTDIR\profile\build\pinyin_simp.table.bin"
  Delete "$INSTDIR\profile\build\pinyin_simp.prism.bin"
  Delete "$INSTDIR\profile\build\pinyin_simp.reverse.bin"
  Delete "$INSTDIR\profile\build\stroke.table.bin"
  Delete "$INSTDIR\profile\build\stroke.prism.bin"
  Delete "$INSTDIR\profile\build\stroke.reverse.bin"
  RMDir "$INSTDIR\profile\build"
  RMDir "$INSTDIR\profile"
  Delete "$SMPROGRAMS\言舟 YANVY 开发测试\言舟设置.lnk"
  Delete "$SMPROGRAMS\言舟 YANVY 开发测试\启动言舟引擎.lnk"
  Delete "$SMPROGRAMS\言舟 YANVY 开发测试\测试说明.lnk"
  Delete "$SMPROGRAMS\言舟 YANVY 开发测试\卸载.lnk"
  RMDir "$SMPROGRAMS\言舟 YANVY 开发测试"
  DeleteRegKey HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\YANVY-dev"
  Delete /REBOOTOK "$INSTDIR\Uninstall.exe"
  Delete /REBOOTOK "$INSTDIR\Uninstall-core.exe"
  RMDir "$INSTDIR"
SectionEnd
