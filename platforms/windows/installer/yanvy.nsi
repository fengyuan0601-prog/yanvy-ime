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
!include "${STAGE}-version.nsh"
Name "言舟 · YANVY ${YANVY_VERSION}"
OutFile "${OUTPUT}"
InstallDir "$LOCALAPPDATA\Programs\YANVY\${YANVY_VERSION}"
; The public native bootstrap requests UAC, sets a protected temp, then launches this
; internal payload with its elevated token and inherited environment.
RequestExecutionLevel user
SetCompressor /SOLID lzma
SetDatablockOptimize on
CRCCheck force
BrandingText "IDEORIN · 开发测试版 / 未签名"
Icon "..\..\..\assets\brand\yanvy.ico"
UninstallIcon "..\..\..\assets\brand\yanvy.ico"
ShowInstDetails show
ShowUninstDetails show
VIProductVersion "${YANVY_WINDOWS_VERSION}"
VIAddVersionKey /LANG=1033 "ProductName" "YANVY"
VIAddVersionKey /LANG=1033 "FileDescription" "YANVY Windows x64 developer test installer"
VIAddVersionKey /LANG=1033 "FileVersion" "${YANVY_VERSION}"
VIAddVersionKey /LANG=1033 "LegalCopyright" "YANVY contributors; upstream licenses preserved"
LicenseData "${STAGE}\TESTING.txt"
LicenseText "请先阅读本测试包的范围、操作和卸载说明。继续安装即表示你已了解这是开发测试版。"
Page license
Page directory
Page instfiles
UninstPage uninstConfirm
UninstPage instfiles
Var PreviousDir
Var PreviousVersion
Var RegistrationAttempted
Var FailureCode
Var RemovalFailed

; Migrate bounded preferences only. Never copy an old Rime
; profile, dictionaries, caches or arbitrary keys into the new installation.
Function MigrateAppearance
  ${If} $PreviousDir == ""
    Return
  ${EndIf}
  IfFileExists "$PreviousDir\YANVY-INSTALL-MARKER.txt" 0 migration_done
  ReadINIStr $0 "$PreviousDir\settings.ini" "display" "font_size"
  IntFmt $1 "%u" $0
  ${If} $0 == $1
  ${AndIf} $0 >= 12
  ${AndIf} $0 <= 32
    WriteINIStr "$INSTDIR\settings.ini" "display" "font_size" $0
  ${EndIf}
  ReadINIStr $0 "$PreviousDir\settings.ini" "display" "theme"
  IntFmt $1 "%u" $0
  ${If} $0 == $1
  ${AndIf} $0 >= 0
  ${AndIf} $0 <= 2
    WriteINIStr "$INSTDIR\settings.ini" "display" "theme" $0
  ${EndIf}
  ReadINIStr $0 "$PreviousDir\settings.ini" "input" "schema"
  IntFmt $1 "%u" $0
  ${If} $0 == $1
  ${AndIf} $0 >= 0
  ${AndIf} $0 <= 4
    WriteINIStr "$INSTDIR\settings.ini" "input" "schema" $0
  ${EndIf}
  ReadINIStr $0 "$PreviousDir\settings.ini" "input" "pinyin_schema"
  IntFmt $1 "%u" $0
  ${If} $0 == $1
  ${AndIf} $0 >= 1
  ${AndIf} $0 <= 4
    WriteINIStr "$INSTDIR\settings.ini" "input" "pinyin_schema" $0
  ${EndIf}
  ReadINIStr $0 "$PreviousDir\settings.ini" "input" "double_schema"
  IntFmt $1 "%u" $0
  ${If} $0 == $1
  ${AndIf} $0 >= 2
  ${AndIf} $0 <= 4
    WriteINIStr "$INSTDIR\settings.ini" "input" "double_schema" $0
  ${EndIf}
  migration_done:
FunctionEnd

Function .onInit
  ReadEnvStr $0 "YANVY_SETUP_TEMP_READY"
  ${If} $0 != "1"
    SetErrorLevel 40
    IfSilent +2
    MessageBox MB_OK|MB_ICONSTOP "请运行对外提供的 YANVY 安装包入口。此文件是内部安装载荷。"
    Abort
  ${EndIf}
  ${IfNot} ${IsNativeAMD64}
    SetErrorLevel 50
    IfSilent +2
    MessageBox MB_OK|MB_ICONSTOP "此测试包只支持 Windows x64，不支持 32 位或 ARM64 Windows。"
    Abort
  ${EndIf}
  SetRegView 64
  SetShellVarContext all
  ReadRegStr $PreviousDir HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\YANVY-dev" "InstallLocation"
  ReadRegStr $PreviousVersion HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\YANVY-dev" "DisplayVersion"
FunctionEnd

Section "言舟 Windows TSF 开发测试"
  StrCpy $RegistrationAttempted 0
  StrCpy $FailureCode 20
  SetRegView 64
  SetShellVarContext all
  ; Never replace an arbitrary directory that merely happens to share a name.
  IfFileExists "$INSTDIR\YANVY-INSTALL-MARKER.txt" existing_owned
  IfFileExists "$INSTDIR\*.*" 0 install_files
    SetErrorLevel 10
    IfSilent +2
    MessageBox MB_OK|MB_ICONSTOP "请选择新的空目录。安装程序不会覆盖已有目录中的文件。"
    Abort
  existing_owned:
    SetErrorLevel 10
    IfSilent +2
    MessageBox MB_OK|MB_ICONINFORMATION "这个开发版本已经安装。请先卸载，或选择新的空目录；旧配置不会被静默覆盖。"
    Abort
  install_files:
  ClearErrors
  SetOutPath "$INSTDIR"
  File /r "${STAGE}\*.*"
  IfErrors rollback
  ; Warm the fixed data using synthetic input before system registration.
  ExecWait '"$INSTDIR\YANVY-Server.exe" --check' $0
  IfErrors rollback
  ${If} $0 != 0
    IfSilent +2
    MessageBox MB_OK|MB_ICONSTOP "固定词库或隐私策略校验失败，未启用言舟。请保留此错误类别并报告。"
    Goto rollback
  ${EndIf}
  ClearErrors
  WriteUninstaller "$INSTDIR\Uninstall-core.exe"
  IfErrors rollback
  StrCpy $FailureCode 30
  StrCpy $RegistrationAttempted 1
  ClearErrors
  ExecWait '"$INSTDIR\YANVY-Setup.exe" --register --machine' $0
  IfErrors rollback
  ${If} $0 != 0
    IfSilent +2
    MessageBox MB_OK|MB_ICONSTOP "Windows TSF 注册失败。请确认已允许管理员授权；安装程序将回退此次注册。"
    Goto rollback
  ${EndIf}
  Call MigrateAppearance
  ReadEnvStr $0 "YANVY_SETUP_SCHEMA"
  IntFmt $1 "%u" $0
  ${If} $0 == $1
  ${AndIf} $0 >= 0
  ${AndIf} $0 <= 4
    WriteINIStr "$INSTDIR\settings.ini" "input" "schema" $0
    ${If} $0 > 0
      WriteINIStr "$INSTDIR\settings.ini" "input" "pinyin_schema" $0
      ${If} $0 >= 2
        WriteINIStr "$INSTDIR\settings.ini" "input" "double_schema" $0
      ${EndIf}
    ${EndIf}
  ${EndIf}
  ClearErrors
  CreateDirectory "$SMPROGRAMS\言舟 YANVY 开发测试"
  CreateShortcut "$SMPROGRAMS\言舟 YANVY 开发测试\言舟设置.lnk" "$INSTDIR\YANVY-Settings.exe"
  CreateShortcut "$SMPROGRAMS\言舟 YANVY 开发测试\启动言舟引擎.lnk" "$INSTDIR\YANVY-Server.exe" "--serve"
  CreateShortcut "$SMPROGRAMS\言舟 YANVY 开发测试\测试说明.lnk" "$INSTDIR\TESTING.txt"
  CreateShortcut "$SMPROGRAMS\言舟 YANVY 开发测试\卸载.lnk" "$INSTDIR\Uninstall.exe"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\YANVY-dev" "DisplayName" "言舟 · YANVY ${YANVY_VERSION}"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\YANVY-dev" "DisplayVersion" "${YANVY_VERSION}"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\YANVY-dev" "Publisher" "IDEORIN / fengyuan0601-prog"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\YANVY-dev" "InstallLocation" "$INSTDIR"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\YANVY-dev" "UninstallString" '$\"$INSTDIR\Uninstall.exe$\"'
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\YANVY-dev" "DisplayIcon" "$INSTDIR\YANVY-Settings.exe"
  WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\YANVY-dev" "NoModify" 1
  WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\YANVY-dev" "NoRepair" 1
  IfErrors rollback
  DetailPrint "安装完成。请从开始菜单启动言舟引擎，再在正常应用里选择言舟输入法。"
  DetailPrint "未更改系统默认输入法。测试前请重新打开目标应用。"
  DetailPrint "升级后请使用本版本的卸载入口，不要运行旧版本文件夹里的卸载程序。"
  SetErrorLevel 0
  Goto done
  rollback:
    ; Do not unregister the working old version if extraction or validation
    ; failed before attempting registration of the new version.
    ${If} $RegistrationAttempted == 1
      StrCpy $RemovalFailed 0
      ClearErrors
      ExecWait '"$INSTDIR\YANVY-Setup.exe" --unregister --machine' $0
      ${If} ${Errors}
        StrCpy $RemovalFailed 1
      ${ElseIf} $0 != 0
        StrCpy $RemovalFailed 1
      ${EndIf}
      ${If} $PreviousDir != ""
        IfFileExists "$PreviousDir\YANVY-Setup.exe" 0 rollback_incomplete
        ClearErrors
        ExecWait '"$PreviousDir\YANVY-Setup.exe" --register --machine' $0
        IfErrors rollback_incomplete
        ${If} $0 != 0
          Goto rollback_incomplete
        ${EndIf}
        ; Only claim recovery after the previous registration succeeded. If
        ; restoration is rejected retain both versions for a deliberate repair.
        ClearErrors
        WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\YANVY-dev" "InstallLocation" "$PreviousDir"
        WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\YANVY-dev" "DisplayVersion" "$PreviousVersion"
        WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\YANVY-dev" "DisplayName" "言舟 · YANVY $PreviousVersion"
        WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\YANVY-dev" "UninstallString" '$\"$PreviousDir\Uninstall.exe$\"'
        WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\YANVY-dev" "DisplayIcon" "$PreviousDir\YANVY-Settings.exe"
        CreateDirectory "$SMPROGRAMS\言舟 YANVY 开发测试"
        CreateShortcut "$SMPROGRAMS\言舟 YANVY 开发测试\言舟设置.lnk" "$PreviousDir\YANVY-Settings.exe"
        CreateShortcut "$SMPROGRAMS\言舟 YANVY 开发测试\启动言舟引擎.lnk" "$PreviousDir\YANVY-Server.exe" "--serve"
        CreateShortcut "$SMPROGRAMS\言舟 YANVY 开发测试\测试说明.lnk" "$PreviousDir\TESTING.txt"
        CreateShortcut "$SMPROGRAMS\言舟 YANVY 开发测试\卸载.lnk" "$PreviousDir\Uninstall.exe"
        IfErrors rollback_incomplete
      ${Else}
        ${If} $RemovalFailed == 1
          Goto rollback_incomplete
        ${EndIf}
        ClearErrors
        DeleteRegKey HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\YANVY-dev"
        Delete "$SMPROGRAMS\言舟 YANVY 开发测试\言舟设置.lnk"
        Delete "$SMPROGRAMS\言舟 YANVY 开发测试\启动言舟引擎.lnk"
        Delete "$SMPROGRAMS\言舟 YANVY 开发测试\测试说明.lnk"
        Delete "$SMPROGRAMS\言舟 YANVY 开发测试\卸载.lnk"
        RMDir "$SMPROGRAMS\言舟 YANVY 开发测试"
      ${EndIf}
    ${EndIf}
    !include "${STAGE}-uninstall.nsh"
    Delete "$INSTDIR\Uninstall-core.exe"
    RMDir "$INSTDIR"
    SetErrorLevel $FailureCode
    Abort
  rollback_incomplete:
    ; The registered COM path may still refer to this installation. Do not
    ; remove its DLL/repair tools or report the ordinary recovered failure.
    SetErrorLevel 31
    IfSilent +2
    MessageBox MB_OK|MB_ICONSTOP "安装失败，且 Windows 未完成回退。新旧程序文件已保留，请保留安装目录并报告错误类别 31。"
    Abort
  done:
SectionEnd

Function un.onInit
  ReadEnvStr $0 "YANVY_SETUP_TEMP_READY"
  ${If} $0 != "1"
    SetErrorLevel 40
    IfSilent +2
    MessageBox MB_OK|MB_ICONSTOP "请运行安装目录的 Uninstall.exe 卸载入口。"
    Abort
  ${EndIf}
  SetRegView 64
  SetShellVarContext all
  ReadRegStr $0 HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\YANVY-dev" "InstallLocation"
  ${If} $0 != ""
  ${AndIf} $0 != $INSTDIR
    SetErrorLevel 50
    IfSilent +2
    MessageBox MB_OK|MB_ICONSTOP "系统当前注册的是另一个言舟版本。请从 Windows 已安装应用中卸载当前版本，以免影响正在使用的输入法。"
    Abort
  ${EndIf}
  IfFileExists "$INSTDIR\YANVY-INSTALL-MARKER.txt" marker_found
    SetErrorLevel 50
    IfSilent +2
    MessageBox MB_OK|MB_ICONSTOP "未找到言舟安装标记，停止卸载以保护其他目录。"
    Abort
  marker_found:
FunctionEnd

Section "Uninstall"
  SetRegView 64
  SetShellVarContext all
  ; Unregister first. Do not remove files if Windows rejects the unregister.
  ExecWait '"$INSTDIR\YANVY-Setup.exe" --unregister --machine' $0
  IfErrors unregister_failed
  ${If} $0 != 0
    unregister_failed:
    SetErrorLevel 30
    IfSilent +2
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
  Delete "$INSTDIR\profile\build\double_pinyin_flypy.schema.yaml"
  Delete "$INSTDIR\profile\build\double_pinyin_flypy.prism.bin"
  Delete "$INSTDIR\profile\build\double_pinyin.schema.yaml"
  Delete "$INSTDIR\profile\build\double_pinyin.prism.bin"
  Delete "$INSTDIR\profile\build\double_pinyin_mspy.schema.yaml"
  Delete "$INSTDIR\profile\build\double_pinyin_mspy.prism.bin"
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
  IfRebootFlag 0 +3
    SetErrorLevel 3010
    Goto uninstall_done
  SetErrorLevel 0
  uninstall_done:
SectionEnd
