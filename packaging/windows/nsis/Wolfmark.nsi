Unicode true
ManifestDPIAware true
RequestExecutionLevel admin
SilentInstall silent
AutoCloseWindow true
SetCompressor /SOLID lzma
SetCompressorDictSize 64
CRCCheck force

!include "FileFunc.nsh"
!include "LogicLib.nsh"
!include "x64.nsh"

!ifndef OUTPUT_EXE
  !error "OUTPUT_EXE is required"
!endif
!ifndef SETUP_RUNTIME
  !error "SETUP_RUNTIME is required"
!endif
!ifndef PAYLOAD_ROOT
  !error "PAYLOAD_ROOT is required"
!endif
!ifndef DISPLAY_VERSION
  !error "DISPLAY_VERSION is required"
!endif
!ifndef WINDOWS_VERSION
  !error "WINDOWS_VERSION is required"
!endif

Name "Wolfmark"
Caption "Wolfmark Setup"
BrandingText "Wolfmark"
OutFile "${OUTPUT_EXE}"
InstallDir "$PROGRAMFILES64\Wolfmark"
InstallDirRegKey HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\Wolfmark" "InstallLocation"
Icon "..\..\..\assets\icons\wolfmark.ico"
UninstallIcon "..\..\..\assets\icons\wolfmark.ico"
VIProductVersion "${WINDOWS_VERSION}"
VIAddVersionKey /LANG=1033 "ProductName" "Wolfmark"
VIAddVersionKey /LANG=1033 "FileDescription" "Wolfmark Setup"
VIAddVersionKey /LANG=1033 "CompanyName" "ItsW0lfiy"
VIAddVersionKey /LANG=1033 "LegalCopyright" "Wolfmark contributors"
VIAddVersionKey /LANG=1033 "FileVersion" "${DISPLAY_VERSION}"
VIAddVersionKey /LANG=1033 "ProductVersion" "${DISPLAY_VERSION}"

Var SetupAction
Var SetupSilent
Var SetupExitCode

Function DetermineAction
  StrCpy $SetupAction ""
  ${GetOptions} $CMDLINE "/MAINTENANCE" $0
  ${IfNot} ${Errors}
    StrCpy $SetupAction "maintenance"
  ${EndIf}
  ClearErrors
  ${GetOptions} $CMDLINE "/UPDATE" $0
  ${IfNot} ${Errors}
    StrCpy $SetupAction "update"
  ${EndIf}
  ClearErrors
  ${GetOptions} $CMDLINE "/REPAIR" $0
  ${IfNot} ${Errors}
    StrCpy $SetupAction "repair"
  ${EndIf}
  ClearErrors
  ${GetOptions} $CMDLINE "/REINSTALL" $0
  ${IfNot} ${Errors}
    StrCpy $SetupAction "reinstall"
  ${EndIf}
  ClearErrors
  ${GetOptions} $CMDLINE "/UNINSTALL" $0
  ${IfNot} ${Errors}
    StrCpy $SetupAction "uninstall"
  ${EndIf}
  ClearErrors
  ${GetOptions} $CMDLINE "/DIAGNOSE" $0
  ${IfNot} ${Errors}
    StrCpy $SetupAction "diagnose"
  ${EndIf}
  IfSilent 0 +2
    StrCpy $SetupSilent "--silent"
FunctionEnd

Function RunWolfmarkSetup
  Call DetermineAction
  ExecWait '"$PLUGINSDIR\setup\WolfmarkSetup.exe" --maintenance="$PLUGINSDIR\setup\WolfmarkMaintenance.exe" --payload="$PLUGINSDIR\payload" --manifest="$PLUGINSDIR\payload\payload-manifest.json" --uninstaller="$PLUGINSDIR\Wolfmark-Uninstall.exe" --install-dir="$INSTDIR" --action=$SetupAction $SetupSilent' $SetupExitCode
  SetErrorLevel $SetupExitCode
FunctionEnd

Section "Wolfmark Setup"
  ${IfNot} ${RunningX64}
    MessageBox MB_ICONSTOP "Wolfmark requires 64-bit Windows."
    SetErrorLevel 1633
    Quit
  ${EndIf}
  SetRegView 64
  SetShellVarContext all
  SetOutPath "$PLUGINSDIR\setup"
  File /r "${SETUP_RUNTIME}\*.*"
  SetOutPath "$PLUGINSDIR\payload"
  File /r "${PAYLOAD_ROOT}\*.*"
  WriteUninstaller "$PLUGINSDIR\Wolfmark-Uninstall.exe"

  ; Remove the final unreleased WiX dev.7 MSI through Windows Installer before
  ; the NSIS maintenance core assumes ownership. Missing products are ignored.
  ReadRegStr $0 HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\{FF340EB2-370A-474F-A6B8-4E78AFAE8952}" "DisplayName"
  ${If} $0 != ""
    ExecWait '"$SYSDIR\msiexec.exe" /x {FF340EB2-370A-474F-A6B8-4E78AFAE8952} /qn /norestart' $1
  ${EndIf}
  Call RunWolfmarkSetup
SectionEnd

Section "Uninstall"
  SetRegView 64
  SetShellVarContext all
  SetOutPath "$PLUGINSDIR\setup"
  File /r "${SETUP_RUNTIME}\*.*"
  SetOutPath "$PLUGINSDIR\payload"
  File /r "${PAYLOAD_ROOT}\*.*"
  StrCpy $SetupAction "uninstall"
  IfSilent 0 +2
    StrCpy $SetupSilent "--silent"
  ExecWait '"$PLUGINSDIR\setup\WolfmarkSetup.exe" --maintenance="$PLUGINSDIR\setup\WolfmarkMaintenance.exe" --payload="$PLUGINSDIR\payload" --manifest="$PLUGINSDIR\payload\payload-manifest.json" --uninstaller="$EXEPATH" --install-dir="$PROGRAMFILES64\Wolfmark" --action=uninstall $SetupSilent' $SetupExitCode
  ${If} $SetupExitCode == 0
    Delete /REBOOTOK "$EXEPATH"
  ${EndIf}
  SetErrorLevel $SetupExitCode
SectionEnd
