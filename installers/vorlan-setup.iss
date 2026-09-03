; ============================================================================
;  VorLAN - instalador para Windows (Inno Setup)
; ----------------------------------------------------------------------------
;  Requisitos: Inno Setup 6 (https://jrsoftware.org/isdl.php).
;  Antes de compilar este script, genera la carpeta autocontenida con:
;     build-windows.bat        (produce dist\vorlan-windows\ con vorlan.exe + DLLs)
;  Luego abre este .iss con Inno Setup y pulsa "Compile" (o: ISCC vorlan-setup.iss).
;  Resultado: installers\vorlan-1.01-setup.exe
; ============================================================================

#define MyAppName "VorLAN"
#define MyAppVersion "1.01"
#define MyAppPublisher "VorLAN"
#define MyAppExeName "vorlan.exe"

[Setup]
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
DefaultDirName={autopf}\{#MyAppName}
DefaultGroupName={#MyAppName}
DisableProgramGroupPage=yes
UninstallDisplayIcon={app}\{#MyAppExeName}
OutputDir=.
OutputBaseFilename=vorlan-{#MyAppVersion}-setup
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
ArchitecturesInstallIn64BitMode=x64compatible
SetupIconFile=..\qt\appicon.ico

[Languages]
Name: "spanish"; MessagesFile: "compiler:Languages\Spanish.isl"
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
; Carpeta autocontenida generada por build-windows.bat (vorlan.exe + DLLs/plugins de Qt).
Source: "..\dist\vorlan-windows\*"; DestDir: "{app}"; Flags: recursesubdirs createallsubdirs ignoreversion

[Icons]
Name: "{group}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; Tasks: desktopicon

[Run]
; Regla de firewall para el descubrimiento y las transferencias en la red local.
Filename: "netsh"; Parameters: "advfirewall firewall add rule name=""VorLAN"" dir=in action=allow program=""{app}\{#MyAppExeName}"" enable=yes profile=private,domain"; Flags: runhidden
Filename: "{app}\{#MyAppExeName}"; Description: "{cm:LaunchProgram,{#MyAppName}}"; Flags: nowait postinstall skipifsilent

[UninstallRun]
Filename: "netsh"; Parameters: "advfirewall firewall delete rule name=""VorLAN"""; Flags: runhidden; RunOnceId: "DelFirewallRule"
