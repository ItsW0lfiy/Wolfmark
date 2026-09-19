#ifndef MyAppVersion
  #error MyAppVersion must be supplied by the release script.
#endif
#ifndef MyNumericVersion
  #error MyNumericVersion must be supplied by the release script.
#endif
#ifndef SourceDir
  #error SourceDir must point to the staged Moonmark payload.
#endif
#ifndef OutputDir
  #error OutputDir must point to the versioned release directory.
#endif
#ifndef ProjectRoot
  #error ProjectRoot must point to the Moonmark repository root.
#endif

#define MyAppName "Moonmark"
#define MyAppPublisher "ItsW0lfiy"
#define MyAppExeName "Moonmark.exe"

[Setup]
AppId={{42E4C993-27F8-45C4-BF30-7360F9349DCA}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppVerName={#MyAppName} {#MyAppVersion}
AppPublisher={#MyAppPublisher}
AppPublisherURL=https://github.com/Wolfyisdabest/Moonmark
AppSupportURL=https://github.com/Wolfyisdabest/Moonmark/issues
AppUpdatesURL=https://github.com/Wolfyisdabest/Moonmark/releases
VersionInfoVersion={#MyNumericVersion}
VersionInfoProductVersion={#MyNumericVersion}
VersionInfoTextVersion={#MyAppVersion}
VersionInfoCompany={#MyAppPublisher}
VersionInfoDescription=Moonmark Markdown Viewer Setup
VersionInfoProductName={#MyAppName}
DefaultDirName={autopf}\Moonmark
DefaultGroupName=Moonmark
DisableProgramGroupPage=yes
UninstallDisplayName={#MyAppName}
UninstallDisplayIcon={app}\{#MyAppExeName}
OutputDir={#OutputDir}
OutputBaseFilename=Moonmark-Setup-win-x64
SetupIconFile={#ProjectRoot}\assets\icons\moonmark.ico
LicenseFile={#SourceDir}\LICENSE
Compression=lzma2/ultra64
SolidCompression=yes
WizardStyle=modern
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
PrivilegesRequiredOverridesAllowed=commandline
ChangesAssociations=yes
CloseApplications=yes
RestartApplications=no
UsePreviousAppDir=yes
UsePreviousGroup=yes
UsePreviousTasks=yes
AllowNoIcons=yes
SetupLogging=yes
MinVersion=10.0.17763

[Tasks]
Name: "desktopicon"; Description: "Create a &Desktop shortcut"; GroupDescription: "Additional shortcuts:"; Flags: unchecked
Name: "fileassoc"; Description: "Register Moonmark in Windows &Open With menus"; GroupDescription: "Windows integration:"; Flags: checkedonce

[Files]
Source: "{#SourceDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{autoprograms}\Moonmark\Moonmark"; Filename: "{app}\{#MyAppExeName}"; WorkingDir: "{app}"; IconFilename: "{app}\{#MyAppExeName}"
Name: "{autodesktop}\Moonmark"; Filename: "{app}\{#MyAppExeName}"; WorkingDir: "{app}"; IconFilename: "{app}\{#MyAppExeName}"; Tasks: desktopicon

[Registry]
Root: HKA; Subkey: "Software\RegisteredApplications"; ValueType: string; ValueName: "Moonmark"; ValueData: "Software\ItsW0lfiy\Moonmark\Capabilities"; Flags: uninsdeletevalue; Tasks: fileassoc
Root: HKA; Subkey: "Software\ItsW0lfiy\Moonmark"; Flags: uninsdeletekeyifempty; Tasks: fileassoc
Root: HKA; Subkey: "Software\ItsW0lfiy\Moonmark\Capabilities"; ValueType: string; ValueName: "ApplicationName"; ValueData: "Moonmark"; Flags: uninsdeletekey; Tasks: fileassoc
Root: HKA; Subkey: "Software\ItsW0lfiy\Moonmark\Capabilities"; ValueType: string; ValueName: "ApplicationDescription"; ValueData: "Native Markdown and text document viewer"; Tasks: fileassoc
Root: HKA; Subkey: "Software\ItsW0lfiy\Moonmark\Capabilities"; ValueType: string; ValueName: "ApplicationIcon"; ValueData: "{app}\{#MyAppExeName},0"; Tasks: fileassoc
Root: HKA; Subkey: "Software\ItsW0lfiy\Moonmark\Capabilities\FileAssociations"; ValueType: string; ValueName: ".md"; ValueData: "Moonmark.MarkdownDocument"; Tasks: fileassoc
Root: HKA; Subkey: "Software\ItsW0lfiy\Moonmark\Capabilities\FileAssociations"; ValueType: string; ValueName: ".markdown"; ValueData: "Moonmark.MarkdownDocument"; Tasks: fileassoc
Root: HKA; Subkey: "Software\ItsW0lfiy\Moonmark\Capabilities\FileAssociations"; ValueType: string; ValueName: ".txt"; ValueData: "Moonmark.TextDocument"; Tasks: fileassoc

Root: HKA; Subkey: "Software\Classes\Moonmark.Document"; Flags: deletekey; Tasks: fileassoc
Root: HKA; Subkey: "Software\Classes\Moonmark.MarkdownDocument"; ValueType: string; ValueData: "Moonmark Markdown Document"; Flags: uninsdeletekey; Tasks: fileassoc
Root: HKA; Subkey: "Software\Classes\Moonmark.MarkdownDocument\DefaultIcon"; ValueType: string; ValueData: "{app}\assets\icons\moonmark-markdown.ico"; Tasks: fileassoc
Root: HKA; Subkey: "Software\Classes\Moonmark.MarkdownDocument\shell\open\command"; ValueType: string; ValueData: """{app}\{#MyAppExeName}"" ""%1"""; Tasks: fileassoc
Root: HKA; Subkey: "Software\Classes\Moonmark.TextDocument"; ValueType: string; ValueData: "Moonmark Text Document"; Flags: uninsdeletekey; Tasks: fileassoc
Root: HKA; Subkey: "Software\Classes\Moonmark.TextDocument\DefaultIcon"; ValueType: string; ValueData: "{app}\assets\icons\moonmark-text.ico"; Tasks: fileassoc
Root: HKA; Subkey: "Software\Classes\Moonmark.TextDocument\shell\open\command"; ValueType: string; ValueData: """{app}\{#MyAppExeName}"" ""%1"""; Tasks: fileassoc

Root: HKA; Subkey: "Software\Classes\Applications\Moonmark.exe"; ValueType: string; ValueName: "FriendlyAppName"; ValueData: "Moonmark"; Flags: uninsdeletekey; Tasks: fileassoc
Root: HKA; Subkey: "Software\Classes\Applications\Moonmark.exe\DefaultIcon"; ValueType: string; ValueData: "{app}\{#MyAppExeName},0"; Tasks: fileassoc
Root: HKA; Subkey: "Software\Classes\Applications\Moonmark.exe\shell\open\command"; ValueType: string; ValueData: """{app}\{#MyAppExeName}"" ""%1"""; Tasks: fileassoc
Root: HKA; Subkey: "Software\Classes\Applications\Moonmark.exe\SupportedTypes"; ValueType: string; ValueName: ".md"; ValueData: ""; Tasks: fileassoc
Root: HKA; Subkey: "Software\Classes\Applications\Moonmark.exe\SupportedTypes"; ValueType: string; ValueName: ".markdown"; ValueData: ""; Tasks: fileassoc
Root: HKA; Subkey: "Software\Classes\Applications\Moonmark.exe\SupportedTypes"; ValueType: string; ValueName: ".txt"; ValueData: ""; Tasks: fileassoc

Root: HKA; Subkey: "Software\Classes\.md\OpenWithProgids"; ValueType: string; ValueName: "Moonmark.MarkdownDocument"; ValueData: ""; Flags: uninsdeletevalue; Tasks: fileassoc
Root: HKA; Subkey: "Software\Classes\.markdown\OpenWithProgids"; ValueType: string; ValueName: "Moonmark.MarkdownDocument"; ValueData: ""; Flags: uninsdeletevalue; Tasks: fileassoc
Root: HKA; Subkey: "Software\Classes\.txt\OpenWithProgids"; ValueType: string; ValueName: "Moonmark.TextDocument"; ValueData: ""; Flags: uninsdeletevalue; Tasks: fileassoc

[Run]
Filename: "{app}\{#MyAppExeName}"; Description: "Launch Moonmark"; Flags: nowait postinstall skipifsilent
