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
InstallDir "$PROGRAMFILES64\YANVY\${YANVY_VERSION}"
; The public native bootstrap requests UAC, sets a protected temp, then launches this
; internal payload with its elevated token and inherited environment.
RequestExecutionLevel user
SetCompressor /SOLID lzma
SetDatablockOptimize on
CRCCheck force
BrandingText "IDEORIN · 免费开源 / 未签名"
Icon "..\..\..\assets\brand\yanvy.ico"
UninstallIcon "..\..\..\assets\brand\yanvy.ico"
ShowInstDetails show
ShowUninstDetails show
VIProductVersion "${YANVY_WINDOWS_VERSION}"
VIAddVersionKey /LANG=1033 "ProductName" "YANVY"
VIAddVersionKey /LANG=1033 "FileDescription" "YANVY Windows x64 installer"
VIAddVersionKey /LANG=1033 "FileVersion" "${YANVY_VERSION}"
VIAddVersionKey /LANG=1033 "LegalCopyright" "YANVY contributors; upstream licenses preserved"
LicenseData "${STAGE}\TESTING.txt"
LicenseText "言舟免费开源。请阅读安装、使用、卸载和许可证说明。"
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
Var CleanupWarning
Var DataDir
Var LegacyProfile
Var RepairMode
Var RepairBackupReady
Var RepairHadInstallation
Var RepairHadSettings
Var RepairHadUninstaller

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
    MessageBox MB_OK|MB_ICONSTOP "本安装包只支持 Windows x64，不支持 32 位或 ARM64 Windows。"
    Abort
  ${EndIf}
  SetRegView 64
  SetShellVarContext all
  ; The privileged payload never installs code into a user-chosen directory.
  StrCmp $INSTDIR "$PROGRAMFILES64\YANVY\${YANVY_VERSION}" +3
    SetErrorLevel 41
    Abort
  ReadEnvStr $DataDir "YANVY_DATA_LOCATION"
  ReadRegStr $PreviousDir HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\YANVY-dev" "InstallLocation"
  ReadRegStr $PreviousVersion HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\YANVY-dev" "DisplayVersion"
FunctionEnd

Section "言舟 Windows 输入法"
  StrCpy $RegistrationAttempted 0
  StrCpy $RepairMode 0
  StrCpy $RepairBackupReady 0
  StrCpy $LegacyProfile 1
  StrCpy $FailureCode 20
  SetRegView 64
  SetShellVarContext all
  ; Never replace an arbitrary directory that merely happens to share a name.
  IfFileExists "$INSTDIR\YANVY-INSTALL-MARKER.txt" existing_owned
  ; FindFirst returns dot entries for an empty existing directory. The native
  ; worker pre-creates this protected directory, so existence is not nonempty.
  FindFirst $1 $2 "$INSTDIR\*"
  directory_next:
    StrCmp $2 "" directory_empty
    StrCmp $2 "." directory_continue
    StrCmp $2 ".." directory_continue
    FindClose $1
    SetErrorLevel 10
    IfSilent +2
    MessageBox MB_OK|MB_ICONSTOP "请选择新的空目录。安装程序不会覆盖已有目录中的文件。"
    Abort
  directory_continue:
    FindNext $1 $2
    Goto directory_next
  directory_empty:
    FindClose $1
    Goto install_files
  existing_owned:
    ; The protected native worker compares this exact package's embedded
    ; file hashes before granting reuse. Never infer trust from a marker.
    ReadEnvStr $0 "YANVY_REPAIR_VERIFIED"
    ${If} $0 == "1"
      StrCpy $RepairMode 1
      Goto repair_existing
    ${EndIf}
    SetErrorLevel 11
    IfSilent +2
    MessageBox MB_OK|MB_ICONINFORMATION "现有文件未通过当前安装包的完整性验证，已保留原文件。请查看安装诊断。"
    Abort
  repair_existing:
    ; Reconfigure a verified, intact installation without overwriting loaded
    ; binaries. Keep byte-for-byte copies of both metadata files until commit.
    StrCpy $RepairHadInstallation 0
    StrCpy $RepairHadSettings 0
    StrCpy $RepairHadUninstaller 0
    ClearErrors
    InitPluginsDir
    IfErrors repair_failed
    IfFileExists "$INSTDIR\installation.ini" 0 +4
      StrCpy $RepairHadInstallation 1
      CopyFiles /SILENT "$INSTDIR\installation.ini" "$PLUGINSDIR\repair-installation.ini"
      IfErrors repair_failed
    IfFileExists "$INSTDIR\settings.ini" 0 +4
      StrCpy $RepairHadSettings 1
      CopyFiles /SILENT "$INSTDIR\settings.ini" "$PLUGINSDIR\repair-settings.ini"
      IfErrors repair_failed
    IfFileExists "$INSTDIR\Uninstall-core.exe" 0 +4
      StrCpy $RepairHadUninstaller 1
      CopyFiles /SILENT "$INSTDIR\Uninstall-core.exe" "$PLUGINSDIR\repair-uninstall.exe"
      IfErrors repair_failed
    StrCpy $RepairBackupReady 1
    ClearErrors
    ExecWait '"$INSTDIR\YANVY-Setup.exe" --repair-register --machine' $0
    IfErrors repair_failed
    ${If} $0 != 0
      Goto repair_failed
    ${EndIf}
    ; This generated file is not in the staged immutable-file manifest.
    ; Rebuild it from this trusted payload; never execute its previous bytes.
    ClearErrors
    WriteUninstaller "$INSTDIR\Uninstall-core.exe"
    IfErrors repair_failed
    Goto write_metadata
  install_files:
  StrCpy $FailureCode 21
  ClearErrors
  SetOutPath "$INSTDIR"
  File /r "${STAGE}\*.*"
  IfErrors rollback
  ; Detect missing engine DLL/dependencies before the server loader starts.
  StrCpy $FailureCode 23
  ClearErrors
  ExecWait '"$INSTDIR\YANVY-Setup.exe" --check-dependencies' $0
  IfErrors rollback
  ${If} $0 != 0
    Goto rollback
  ${EndIf}
  ; Verify that the 32-bit loader can instantiate the packaged TSF component.
  ClearErrors
  ExecWait '"$INSTDIR\YANVY-Check-x86.exe" "$INSTDIR\YANVY-TSF-x86.dll"' $0
  IfErrors rollback
  ${If} $0 != 0
    Goto rollback
  ${EndIf}
  ; Warm the fixed data using synthetic input before system registration.
  StrCpy $FailureCode 22
  ClearErrors
  ExecWait '"$INSTDIR\YANVY-Server.exe" --check' $0
  IfErrors rollback
  ${If} $0 != 0
    StrCpy $FailureCode 26
    ; Keep Windows exception statuses intact so the outer UI can show the
    ; real reason. This contains no paths, input, or memory contents.
    ${If} $0 < 0
      StrCpy $FailureCode $0
    ${ElseIf} $0 == 5
      StrCpy $FailureCode 24
    ${ElseIf} $0 == 6
      StrCpy $FailureCode 25
    ${ElseIf} $0 == 7
      StrCpy $FailureCode 27
    ${ElseIf} $0 == 8
      StrCpy $FailureCode 28
    ${ElseIf} $0 == 9
      StrCpy $FailureCode 29
    ${ElseIf} $0 == 10
      StrCpy $FailureCode 32
    ${Else}
      ; Preserve ordinary unexpected exit codes too, without colliding with
      ; the installer's own small result categories.
      ${If} $0 <= 65535
        IntOp $FailureCode $0 | 0xA1260000
      ${Else}
        StrCpy $FailureCode $0
      ${EndIf}
    ${EndIf}
    IfSilent +2
    MessageBox MB_OK|MB_ICONSTOP "固定词库或隐私策略校验失败，未启用言舟。请保留此错误类别并报告。"
    Goto rollback
  ${EndIf}
  ClearErrors
  WriteUninstaller "$INSTDIR\Uninstall-core.exe"
  IfErrors rollback
  ; The legacy identity remains intact until every new-install step succeeds.
  ExecWait '"$INSTDIR\YANVY-Setup.exe" --has-legacy-profile' $LegacyProfile
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
  write_metadata:
  ; Data has been prepared or verified by the original, unelevated user. Only this
  ; pointer is stored here; the administrator worker never writes that path.
  ClearErrors
  WriteINIStr "$INSTDIR\installation.ini" "paths" "data_root" "$DataDir"
  IfErrors rollback
  ReadEnvStr $0 "YANVY_SETUP_SCHEMA"
  ; A missing optional schema is not a file-write failure.
  ClearErrors
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
  ; TEST_POINT_AFTER_INSTALL_METADATA
  IfErrors rollback
  ClearErrors
  CreateDirectory "$SMPROGRAMS\言舟 YANVY"
  CreateShortcut "$SMPROGRAMS\言舟 YANVY\言舟设置.lnk" "$INSTDIR\YANVY-Settings.exe"
  CreateShortcut "$SMPROGRAMS\言舟 YANVY\启动言舟引擎.lnk" "$INSTDIR\YANVY-Server.exe" "--serve"
  CreateShortcut "$SMPROGRAMS\言舟 YANVY\使用说明.lnk" "$INSTDIR\TESTING.txt"
  CreateShortcut "$SMPROGRAMS\言舟 YANVY\卸载.lnk" "$INSTDIR\Uninstall.exe"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\YANVY-dev" "DisplayName" "言舟 · YANVY ${YANVY_VERSION}"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\YANVY-dev" "DisplayVersion" "${YANVY_VERSION}"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\YANVY-dev" "Publisher" "IDEORIN / fengyuan0601-prog"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\YANVY-dev" "InstallLocation" "$INSTDIR"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\YANVY-dev" "UninstallString" '$\"$INSTDIR\Uninstall.exe$\"'
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\YANVY-dev" "DisplayIcon" "$INSTDIR\YANVY-Settings.exe"
  WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\YANVY-dev" "NoModify" 1
  WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\YANVY-dev" "NoRepair" 1
  IfErrors rollback
  ; New registration and metadata have succeeded. Only now clean exact owned
  ; files from the previously registered, validated directory. Do not run its
  ; uninstaller: it could unregister the new version. Preserve settings and
  ; unknown files; locked components are scheduled for removal on restart.
  StrCpy $CleanupWarning 0
  ; Retire only the known development shortcuts after a successful install.
  Delete "$SMPROGRAMS\言舟 YANVY 开发测试\言舟设置.lnk"
  Delete "$SMPROGRAMS\言舟 YANVY 开发测试\启动言舟引擎.lnk"
  Delete "$SMPROGRAMS\言舟 YANVY 开发测试\测试说明.lnk"
  Delete "$SMPROGRAMS\言舟 YANVY 开发测试\卸载.lnk"
  RMDir "$SMPROGRAMS\言舟 YANVY 开发测试"
  ClearErrors
  ExecWait '"$INSTDIR\YANVY-Setup.exe" --retire-legacy --machine' $0
  ${If} ${Errors}
    StrCpy $CleanupWarning 1
  ${ElseIf} $0 != 0
    StrCpy $CleanupWarning 1
  ${EndIf}
  ${If} $PreviousDir != ""
  ${AndIf} $PreviousDir != $INSTDIR
  ${AndIf} $CleanupWarning == 0
    ; Failed retirement can leave a previous COM class pointing at its DLL.
    ; Preserve its whole runtime and repair tools until removal can succeed.
    ; Recheck the complete protected path before deleting old machine files.
    ExecWait '"$INSTDIR\YANVY-Setup.exe" --validate-previous-cleanup "$PreviousDir"' $0
    ${If} ${Errors}
      StrCpy $0 1
    ${EndIf}
    ${If} $0 == 0
      IfFileExists "$PreviousDir\YANVY-INSTALL-MARKER.txt" 0 old_cleanup_unsafe
      ClearErrors
      FileOpen $1 "$PreviousDir\YANVY-INSTALL-MARKER.txt" r
      IfErrors old_cleanup_unsafe
      FileRead $1 $2
      FileClose $1
      StrCpy $2 $2 6
      StrCmp $2 "YANVY " 0 old_cleanup_unsafe
      StrCpy $R9 $INSTDIR
      StrCpy $INSTDIR $PreviousDir
      !include "${STAGE}-uninstall.nsh"
      Delete /REBOOTOK "$INSTDIR\installation.ini"
      Delete /REBOOTOK "$INSTDIR\Uninstall-core.exe"
      Delete /REBOOTOK "$INSTDIR\profile\user.yaml"
      Delete /REBOOTOK "$INSTDIR\profile\installation.yaml"
      Delete /REBOOTOK "$INSTDIR\profile\build\default.yaml"
      !macro OldCache schema
        Delete /REBOOTOK "$INSTDIR\profile\build\${schema}.schema.yaml"
        Delete /REBOOTOK "$INSTDIR\profile\build\${schema}.table.bin"
        Delete /REBOOTOK "$INSTDIR\profile\build\${schema}.prism.bin"
        Delete /REBOOTOK "$INSTDIR\profile\build\${schema}.reverse.bin"
      !macroend
      !insertmacro OldCache "wubi86"
      !insertmacro OldCache "pinyin_simp"
      !insertmacro OldCache "stroke"
      !insertmacro OldCache "double_pinyin"
      !insertmacro OldCache "double_pinyin_flypy"
      !insertmacro OldCache "double_pinyin_mspy"
      RMDir "$INSTDIR\profile\build"
      RMDir "$INSTDIR\profile"
      IfFileExists "$INSTDIR\YANVY-Setup.exe" 0 old_cleanup_done
      IfRebootFlag old_cleanup_done 0
      StrCpy $CleanupWarning 1
      old_cleanup_done:
      RMDir "$INSTDIR"
      StrCpy $INSTDIR $R9
      Goto old_cleanup_complete
      old_cleanup_unsafe:
      StrCpy $CleanupWarning 1
      old_cleanup_complete:
    ${Else}
      StrCpy $CleanupWarning 1
    ${EndIf}
  ${EndIf}
  ; Retained old TSF instances still call their original settings path. Only
  ; after the new installation is committed and old cleanup has finished,
  ; restore missing fixed entry points from this trusted settings executable.
  ClearErrors
  ExecWait '"$INSTDIR\YANVY-Setup.exe" --restore-settings-entries --machine' $0
  ${If} ${Errors}
    StrCpy $CleanupWarning 1
  ${ElseIf} $0 != 0
    StrCpy $CleanupWarning 1
  ${EndIf}
  DetailPrint "安装完成。请从开始菜单启动言舟引擎，再在正常应用里选择言舟输入法。"
  DetailPrint "未更改系统默认输入法。请在系统输入法列表中选择言舟。"
  DetailPrint "升级后请使用本版本的卸载入口，不要运行旧版本文件夹里的卸载程序。"
  ${If} $CleanupWarning == 1
    SetErrorLevel 33
  ${Else}
    IfRebootFlag 0 +3
      SetErrorLevel 3010
      Goto done
    SetErrorLevel 0
  ${EndIf}
  Goto done
  rollback:
    ${If} $RepairMode == 1
      Goto repair_failed
    ${EndIf}
    ; Do not unregister the working old version if extraction or validation
    ; failed before attempting registration of the new version.
    ${If} $RegistrationAttempted == 1
      StrCpy $RemovalFailed 0
      ClearErrors
      ExecWait '"$INSTDIR\YANVY-Setup.exe" --unregister-current --machine' $0
      ${If} ${Errors}
        StrCpy $RemovalFailed 1
      ${ElseIf} $0 != 0
        StrCpy $RemovalFailed 1
      ${EndIf}
      ${If} $PreviousDir != ""
        ClearErrors
        ; Restore registration with the CURRENT trusted helper. Never execute
        ; a previous directory's program under this administrator token.
        ${If} $LegacyProfile == 0
          ; The old CLSID/profile was never modified. Do not re-register a
          ; legacy user-writable DLL, and never execute its helper.
          ${If} $RemovalFailed == 1
            Goto rollback_incomplete
          ${EndIf}
        ${Else}
          ExecWait '"$INSTDIR\YANVY-Setup.exe" --restore-registration --machine "$PreviousDir"' $0
          IfErrors rollback_incomplete
          ${If} $0 != 0
            Goto rollback_incomplete
          ${EndIf}
        ${EndIf}
        ; Only claim recovery after the previous registration succeeded. If
        ; restoration is rejected retain both versions for a deliberate repair.
        ClearErrors
        WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\YANVY-dev" "InstallLocation" "$PreviousDir"
        WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\YANVY-dev" "DisplayVersion" "$PreviousVersion"
        WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\YANVY-dev" "DisplayName" "言舟 · YANVY $PreviousVersion"
        WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\YANVY-dev" "UninstallString" '$\"$PreviousDir\Uninstall.exe$\"'
        WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\YANVY-dev" "DisplayIcon" "$PreviousDir\YANVY-Settings.exe"
        CreateDirectory "$SMPROGRAMS\言舟 YANVY"
        CreateShortcut "$SMPROGRAMS\言舟 YANVY\言舟设置.lnk" "$PreviousDir\YANVY-Settings.exe"
        CreateShortcut "$SMPROGRAMS\言舟 YANVY\启动言舟引擎.lnk" "$PreviousDir\YANVY-Server.exe" "--serve"
        CreateShortcut "$SMPROGRAMS\言舟 YANVY\使用说明.lnk" "$PreviousDir\TESTING.txt"
        CreateShortcut "$SMPROGRAMS\言舟 YANVY\卸载.lnk" "$PreviousDir\Uninstall.exe"
        IfErrors rollback_incomplete
      ${Else}
        ${If} $RemovalFailed == 1
          Goto rollback_incomplete
        ${EndIf}
        ClearErrors
        DeleteRegKey HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\YANVY-dev"
        Delete "$SMPROGRAMS\言舟 YANVY\言舟设置.lnk"
        Delete "$SMPROGRAMS\言舟 YANVY\启动言舟引擎.lnk"
        Delete "$SMPROGRAMS\言舟 YANVY\使用说明.lnk"
        Delete "$SMPROGRAMS\言舟 YANVY\卸载.lnk"
        RMDir "$SMPROGRAMS\言舟 YANVY"
      ${EndIf}
    ${EndIf}
    !include "${STAGE}-uninstall.nsh"
    ; --check creates only these fixed deployment caches. Remove them on a
    ; recovered failure so a retry can reuse the protected version directory.
    Delete "$INSTDIR\profile\user.yaml"
    Delete "$INSTDIR\profile\installation.yaml"
    Delete "$INSTDIR\profile\build\default.yaml"
    !macro FailedCache schema
      Delete "$INSTDIR\profile\build\${schema}.schema.yaml"
      Delete "$INSTDIR\profile\build\${schema}.table.bin"
      Delete "$INSTDIR\profile\build\${schema}.prism.bin"
      Delete "$INSTDIR\profile\build\${schema}.reverse.bin"
    !macroend
    !insertmacro FailedCache "wubi86"
    !insertmacro FailedCache "pinyin_simp"
    !insertmacro FailedCache "stroke"
    !insertmacro FailedCache "double_pinyin"
    !insertmacro FailedCache "double_pinyin_flypy"
    !insertmacro FailedCache "double_pinyin_mspy"
    RMDir "$INSTDIR\profile\build"
    RMDir "$INSTDIR\profile"
    Delete "$INSTDIR\installation.ini"
    Delete "$INSTDIR\settings.ini"
    Delete "$INSTDIR\Uninstall-core.exe"
    RMDir "$INSTDIR"
    SetErrorLevel $FailureCode
    Abort
  repair_failed:
    ; Registration repair never unregisters a working profile or deletes its
    ; runtime. A new data pointer must not survive a failed metadata transaction.
    ${If} $RepairBackupReady == 1
      ClearErrors
      ${If} $RepairHadInstallation == 1
        CopyFiles /SILENT "$PLUGINSDIR\repair-installation.ini" "$INSTDIR\installation.ini"
      ${Else}
        Delete "$INSTDIR\installation.ini"
      ${EndIf}
      ${If} $RepairHadSettings == 1
        CopyFiles /SILENT "$PLUGINSDIR\repair-settings.ini" "$INSTDIR\settings.ini"
      ${Else}
        Delete "$INSTDIR\settings.ini"
      ${EndIf}
      ${If} $RepairHadUninstaller == 1
        CopyFiles /SILENT "$PLUGINSDIR\repair-uninstall.exe" "$INSTDIR\Uninstall-core.exe"
      ${Else}
        Delete "$INSTDIR\Uninstall-core.exe"
      ${EndIf}
    ${EndIf}
    ; Conservatively retain newly prepared user data too: a registration or
    ; metadata error can be partial. The UI treats 31 as preservation required.
    SetErrorLevel 31
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
  Delete "$INSTDIR\installation.ini"
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
  Delete "$SMPROGRAMS\言舟 YANVY\言舟设置.lnk"
  Delete "$SMPROGRAMS\言舟 YANVY\启动言舟引擎.lnk"
  Delete "$SMPROGRAMS\言舟 YANVY\使用说明.lnk"
  Delete "$SMPROGRAMS\言舟 YANVY\卸载.lnk"
  RMDir "$SMPROGRAMS\言舟 YANVY"
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
