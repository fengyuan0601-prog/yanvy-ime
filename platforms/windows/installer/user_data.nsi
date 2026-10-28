; SPDX-License-Identifier: BSD-3-Clause
; Ordinary-user data payload: no executable files, UAC, registration, or helper execution.
Unicode true
!include "LogicLib.nsh"
!include "FileFunc.nsh"
!include "TextFunc.nsh"
!ifndef STAGE
  !error "Pass /DSTAGE=clean-staging-directory"
!endif
!ifndef OUTPUT
  !error "Pass /DOUTPUT=output-payload.exe"
!endif
!ifndef CLEANUP
  !error "Pass /DCLEANUP=generated-exact-data-removal.nsh (excluding marker)"
!endif
!ifndef SOURCE_COMMIT
  !error "Pass /DSOURCE_COMMIT=the-committed-stage-revision"
!endif
!include "${STAGE}-version.nsh"
Name "YANVY offline data ${YANVY_VERSION}"
OutFile "${OUTPUT}"
RequestExecutionLevel user
SilentInstall silent
SetCompressor /SOLID lzma
CRCCheck force
InstallDir "$LOCALAPPDATA\YANVY\Data\${YANVY_VERSION}"
Var CleanupMode
Var UnsafePath
Var CleanupFileError

Function .onInit
  ; "user" prevents an elevation request but cannot drop an inherited elevated
  ; token. Refuse elevated or unreadable tokens before inspecting destinations.
  System::Call 'advapi32::OpenProcessToken(p -1, i 0x0008, *p .r0) i.r1'
  ${If} $1 == 0
    Goto forbidden_token
  ${EndIf}
  System::Call 'advapi32::GetTokenInformation(p r0, i 20, *i .r2, i 4, *i .r3) i.r1'
  System::Call 'kernel32::CloseHandle(p r0)'
  ; TEST_POINT_AFTER_ELEVATION_QUERY
  ${If} $1 == 0
  ${OrIf} $2 != 0
    Goto forbidden_token
  ${EndIf}
  StrCpy $CleanupMode 0
  ${GetParameters} $0
  ClearErrors
  ${GetOptions} $0 "/CLEANUP" $1
  ${IfNot} ${Errors}
    StrCpy $CleanupMode 1
  ${EndIf}
  Return
  forbidden_token:
  SetErrorLevel 40
  Quit
FunctionEnd

; Inspect links before either writing or exact-file deletion. This prevents
; following an existing junction; it is not a sandbox against a concurrent
; process that can already rewrite the same user's directories.
Function InspectTree
  Exch $0
  Push $1
  Push $2
  Push $3
  FindFirst $1 $2 "$0\*"
  tree_next:
    ${If} $2 == ""
      Goto tree_done
    ${EndIf}
    ${If} $2 != "."
    ${AndIf} $2 != ".."
      System::Call 'kernel32::GetFileAttributesW(w "$0\$2") i.r3'
      ${If} $3 == -1
        StrCpy $UnsafePath 1
        Goto tree_done
      ${EndIf}
      IntOp $3 $3 & 0x400
      ${If} $3 != 0
        StrCpy $UnsafePath 1
        Goto tree_done
      ${EndIf}
      System::Call 'kernel32::GetFileAttributesW(w "$0\$2") i.r3'
      IntOp $3 $3 & 0x10
      ${If} $3 != 0
        Push "$0\$2"
        Call InspectTree
        ${If} $UnsafePath != 0
          Goto tree_done
        ${EndIf}
      ${EndIf}
    ${EndIf}
    FindNext $1 $2
    Goto tree_next
  tree_done:
  FindClose $1
  Pop $3
  Pop $2
  Pop $1
  Pop $0
FunctionEnd

Function ValidateDestination
  StrCpy $UnsafePath 0
  System::Call 'kernel32::GetFullPathNameW(w "$INSTDIR", i ${NSIS_MAX_STRLEN}, w .r0, p 0) i.r1'
  ${If} $1 == 0
  ${OrIf} $1 >= ${NSIS_MAX_STRLEN}
    StrCpy $UnsafePath 1
    Return
  ${EndIf}
  StrCpy $INSTDIR $0
  StrLen $0 $INSTDIR
  ${If} $0 < 4
    StrCpy $UnsafePath 1
    Return
  ${EndIf}
  StrCpy $0 $INSTDIR 2 1
  ${If} $0 != ":\"
    StrCpy $UnsafePath 1
    Return
  ${EndIf}
  StrCpy $0 $INSTDIR 3
  System::Call 'kernel32::GetDriveTypeW(w r0) i.r1'
  ${If} $1 != 2
  ${AndIf} $1 != 3
    StrCpy $UnsafePath 1
    Return
  ${EndIf}
  System::Call 'kernel32::GetFileAttributesW(w "$INSTDIR") i.r1'
  ${If} $1 != -1
    IntOp $1 $1 & 0x10
    ${If} $1 == 0
      StrCpy $UnsafePath 1
      Return
    ${EndIf}
  ${EndIf}
  StrCpy $0 $INSTDIR
  ancestor_next:
    System::Call 'kernel32::GetFileAttributesW(w r0) i.r1'
    ${If} $1 != -1
      IntOp $1 $1 & 0x400
      ${If} $1 != 0
        StrCpy $UnsafePath 1
        Return
      ${EndIf}
    ${EndIf}
    ${GetParent} "$0" $1
    ${If} $1 == ""
    ${OrIf} $1 == $0
      Return
    ${EndIf}
    StrCpy $0 $1
    Goto ancestor_next
FunctionEnd

Section
  Call ValidateDestination
  ${If} $UnsafePath != 0
    Goto refused
  ${EndIf}
  ${If} $CleanupMode == 1
    Goto cleanup_existing
  ${EndIf}
  ; Explicitly accept an empty directory, but never overwrite existing data.
  FindFirst $0 $1 "$INSTDIR\*"
  empty_next:
    ${If} $1 == ""
      FindClose $0
      Goto extract
    ${EndIf}
    ${If} $1 != "."
    ${AndIf} $1 != ".."
      FindClose $0
      Goto refused
    ${EndIf}
    FindNext $0 $1
    Goto empty_next
  extract:
  SetOverwrite off
  ClearErrors
  SetOutPath "$INSTDIR\data"
  File "${STAGE}\data\*.yaml"
  IfErrors install_failed
  SetOutPath "$INSTDIR\profile"
  File "${STAGE}\profile\*.yaml"
  IfErrors install_failed
  SetOutPath "$INSTDIR\licenses"
  File /r /x "*.exe" /x "*.dll" /x "*.com" /x "*.bat" /x "*.cmd" /x "*.ps1" /x "*.vbs" /x "*.js" "${STAGE}\licenses\*.*"
  IfErrors install_failed
  SetOutPath "$INSTDIR"
  File "${STAGE}\TESTING.txt"
  File "${STAGE}\BUILD-INFO.json"
  File "${STAGE}\YANVY-INSTALL-MARKER.txt"
  IfErrors install_failed
  ; TEST_POINT_AFTER_EXTRACTION
  SetErrorLevel 0
  Quit

  cleanup_existing:
  ClearErrors
  FileOpen $0 "$INSTDIR\YANVY-INSTALL-MARKER.txt" r
  IfErrors refused
  FileRead $0 $1
  FileRead $0 $2
  FileRead $0 $3
  FileClose $0
  ${TrimNewLines} $1 $1
  ${TrimNewLines} $2 $2
  ${If} $1 != "YANVY ${YANVY_VERSION}"
  ${OrIf} $2 != "${SOURCE_COMMIT}"
  ${OrIf} $3 != ""
    Goto refused
  ${EndIf}
  Push "$INSTDIR"
  Call InspectTree
  ${If} $UnsafePath != 0
    Goto refused
  ${EndIf}
  ClearErrors
  StrCpy $CleanupFileError 0
  !include "${CLEANUP}"
  ${If} $CleanupFileError != 0
    Goto cleanup_failed
  ${EndIf}
  Delete "$INSTDIR\YANVY-INSTALL-MARKER.txt"
  IfErrors cleanup_failed
  ; Unknown files/directories are retained; a non-empty root is not an error.
  RMDir "$INSTDIR"
  SetErrorLevel 0
  Quit

  install_failed:
  ; The destination was empty at entry; only exact packaged files are removed.
  ; Preserve unknown files even if another process created them during setup.
  Push "$INSTDIR"
  Call InspectTree
  ${If} $UnsafePath == 0
    !include "${CLEANUP}"
    Delete "$INSTDIR\YANVY-INSTALL-MARKER.txt"
    RMDir "$INSTDIR"
  ${EndIf}
  cleanup_failed:
  SetErrorLevel 21
  Quit
  refused:
  SetErrorLevel 10
  Quit
SectionEnd
