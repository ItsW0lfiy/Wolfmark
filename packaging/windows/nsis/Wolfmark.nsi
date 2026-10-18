Unicode true
ManifestDPIAware true
RequestExecutionLevel admin
SilentInstall normal
SilentUnInstall normal
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
Var BootstrapLog
Var SetupCommand
Var SetupUninstaller

!macro Log Message
  ${GetTime} "" "L" $0 $1 $2 $3 $4 $5 $6
  FileOpen $9 "$BootstrapLog" a
  FileSeek $9 0 END
  FileWriteUTF16LE $9 '$2-$1-$0 $4:$5:$6 ${Message}$\r$\n'
  FileClose $9
!macroend

!macro InitializeBootstrap
  ; Capture the caller's /S before hiding the NSIS extraction window.
  StrCpy $SetupSilent ""
  IfSilent 0 +2
    StrCpy $SetupSilent "--silent"
  SetSilent silent
  InitPluginsDir
  System::Call 'kernel32::GetCurrentProcessId() i.r0'
  StrCpy $BootstrapLog "$TEMP\Wolfmark-Setup-bootstrap-$0.log"
  FileOpen $9 "$BootstrapLog" w
  FileWriteWord $9 0xFEFF
  FileClose $9
  !insertmacro Log 'stage=bootstrap package=$EXEPATH runtime=$PLUGINSDIR caller-mode=$SetupSilent'
!macroend

Function .onInit
  !insertmacro InitializeBootstrap
FunctionEnd

Function un.onInit
  !insertmacro InitializeBootstrap
FunctionEnd

!macro BootstrapFailure Component
  !insertmacro Log 'stage=failure component=${Component} exit=$SetupExitCode'
  ${If} $SetupSilent == ""
    MessageBox MB_OK|MB_ICONSTOP 'Wolfmark Setup could not start or complete.$\r$\n$\r$\nComponent: ${Component}$\r$\nExit code: $SetupExitCode$\r$\nDiagnostic log: $BootstrapLog'
  ${EndIf}
  SetErrorLevel $SetupExitCode
  Quit
!macroend

!macro ExtractRuntime
  ClearErrors
  SetOutPath "$PLUGINSDIR\setup"
  File /r "${SETUP_RUNTIME}\*.*"
  SetOutPath "$PLUGINSDIR\payload"
  File /r "${PAYLOAD_ROOT}\*.*"
  ${If} ${Errors}
    StrCpy $SetupExitCode 1603
    !insertmacro BootstrapFailure "Runtime extraction"
  ${EndIf}
  !insertmacro Log 'stage=extracted runtime=$PLUGINSDIR'
!macroend

!macro LaunchSetup
  ; Only constructed, known arguments are logged; never copy arbitrary $CMDLINE.
  SetOutPath "$PLUGINSDIR\setup"
  StrCpy $SetupCommand '"$PLUGINSDIR\setup\WolfmarkSetup.exe" --maintenance="$PLUGINSDIR\setup\WolfmarkMaintenance.exe" --payload="$PLUGINSDIR\payload" --manifest="$PLUGINSDIR\payload\payload-manifest.json" --uninstaller="$SetupUninstaller" --install-dir="$INSTDIR" --action="$SetupAction" --setup-source="$EXEPATH" --bootstrap-log="$BootstrapLog" $SetupSilent'
  !insertmacro Log 'stage=launch cwd=$OUTDIR command=$SetupCommand'
  StrCpy $SetupExitCode 1603
  ClearErrors
  ExecWait $SetupCommand $SetupExitCode
  ${If} ${Errors}
    ; ExecWait has no child exit code when CreateProcess fails.
    StrCpy $SetupExitCode 1603
    !insertmacro BootstrapFailure "Wolfmark Setup UI (process creation failed)"
  ${EndIf}
  !insertmacro Log 'stage=child-exit component=WolfmarkSetup exit=$SetupExitCode'
  ${If} $SetupExitCode != 0
  ${AndIf} $SetupExitCode != 1602
  ${AndIf} $SetupExitCode != 1223
    !insertmacro BootstrapFailure "Wolfmark Setup UI"
  ${EndIf}
  SetErrorLevel $SetupExitCode
!macroend

Function DetermineAction
  ClearErrors
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
    StrCpy $SetupSilent "--silent"
  ${EndIf}
FunctionEnd

Function RunWolfmarkSetup
  Call DetermineAction
  StrCpy $SetupUninstaller "$PLUGINSDIR\Wolfmark-Uninstall.exe"
  !insertmacro LaunchSetup
FunctionEnd

Section "Wolfmark Setup"
  ${IfNot} ${RunningX64}
    MessageBox MB_ICONSTOP "Wolfmark requires 64-bit Windows."
    SetErrorLevel 1633
    Quit
  ${EndIf}
  SetRegView 64
  SetShellVarContext all
  !insertmacro ExtractRuntime
  ClearErrors
  WriteUninstaller "$PLUGINSDIR\Wolfmark-Uninstall.exe"
  ${If} ${Errors}
    StrCpy $SetupExitCode 1603
    !insertmacro BootstrapFailure "Uninstaller extraction"
  ${EndIf}

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
  !insertmacro ExtractRuntime
  StrCpy $SetupAction "uninstall"
  StrCpy $SetupUninstaller "$EXEPATH"
  StrCpy $INSTDIR "$PROGRAMFILES64\Wolfmark"
  !insertmacro LaunchSetup
  ${If} $SetupExitCode == 0
    Delete /REBOOTOK "$EXEPATH"
  ${EndIf}
  SetErrorLevel $SetupExitCode
SectionEnd
