; Reclaim Windows Installer Script (Inno Setup 6+)
[Setup]
AppName=Reclaim
AppVersion=1.1.0
AppPublisher=Reclaim Open Source
DefaultDirName={autopf}\Reclaim
DefaultGroupName=Reclaim
OutputDir=..\build\installer
OutputBaseFilename=Reclaim-1.1.0-Setup-x64
SetupIconFile=..\assets\reclaim.ico
Compression=lzma2/max
SolidCompression=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=lowest
UninstallDisplayIcon={app}\Reclaim.exe
LicenseFile=..\LICENSE

[Files]
Source: "..\build\win64\Reclaim.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\build\win64\reclaim-cli.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\assets\rules.json"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\README.md"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\LICENSE"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{group}\Reclaim"; Filename: "{app}\Reclaim.exe"
Name: "{autodesktop}\Reclaim"; Filename: "{app}\Reclaim.exe"; Tasks: desktopicon

[Tasks]
Name: "desktopicon"; Description: "Create a desktop shortcut"; GroupDescription: "Additional icons:"
Name: "contextmenu"; Description: "Add 'Analyze with Reclaim' to Windows Explorer context menu"; GroupDescription: "Windows Integration:"

[Registry]
Root: HKCU; Subkey: "Software\Classes\Directory\shell\ReclaimAnalyze"; ValueType: string; ValueName: ""; ValueData: "Analyze with Reclaim"; Flags: uninsdeletekey; Tasks: contextmenu
Root: HKCU; Subkey: "Software\Classes\Directory\shell\ReclaimAnalyze"; ValueType: string; ValueName: "Icon"; ValueData: "{app}\Reclaim.exe"; Tasks: contextmenu
Root: HKCU; Subkey: "Software\Classes\Directory\shell\ReclaimAnalyze\command"; ValueType: string; ValueName: ""; ValueData: """{app}\Reclaim.exe"" --scan ""%V"""; Tasks: contextmenu
Root: HKCU; Subkey: "Software\Classes\Directory\Background\shell\ReclaimAnalyze"; ValueType: string; ValueName: ""; ValueData: "Analyze with Reclaim"; Flags: uninsdeletekey; Tasks: contextmenu
Root: HKCU; Subkey: "Software\Classes\Directory\Background\shell\ReclaimAnalyze"; ValueType: string; ValueName: "Icon"; ValueData: "{app}\Reclaim.exe"; Tasks: contextmenu
Root: HKCU; Subkey: "Software\Classes\Directory\Background\shell\ReclaimAnalyze\command"; ValueType: string; ValueName: ""; ValueData: """{app}\Reclaim.exe"" --scan ""%V"""; Tasks: contextmenu

[Run]
Filename: "{app}\Reclaim.exe"; Description: "Launch Reclaim"; Flags: nowait postinstall skipifsilent
