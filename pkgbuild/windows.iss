#define AppVersion "0.8.1"

[Setup]
AppId={{6914E346-1221-49BD-B234-7071DD230727}
AppName=Lore
AppVersion={#AppVersion}
AppPublisher=MadeBySaints
DefaultDirName={localappdata}\Programs\Lore
DefaultGroupName=Lore
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
UninstallDisplayIcon={app}\lore-desktop.exe
SetupIconFile=lore.ico
OutputDir=..\build\installer
OutputBaseFilename=Lore-Setup-{#AppVersion}
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
CloseApplications=yes
RestartApplications=no
SetupLogging=yes

[Tasks]
Name: "desktopicon"; Description: "Create a desktop shortcut"; Flags: unchecked

[Files]
Source: "..\build\windows\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{userprograms}\Lore"; Filename: "{app}\lore-desktop.exe"; WorkingDir: "{app}"; AppUserModelID: "MadeBySaints.Lore"
Name: "{userdesktop}\Lore"; Filename: "{app}\lore-desktop.exe"; WorkingDir: "{app}"; AppUserModelID: "MadeBySaints.Lore"; Tasks: desktopicon

[Run]
Filename: "{app}\lore-desktop.exe"; Description: "Open Lore and connect your first project"; Flags: nowait postinstall skipifsilent
