; MasterDesk — Windows installer (Inno Setup 6)
;
; Build (from the repo root, after a Release build into .\build):
;   iscc installer\MasterDesk.iss
; Overridables:
;   /DMyAppVersion=1.2.3   version stamped into the installer
;   /DBuildDir=path        plugin artefacts root (default ..\build\MasterDesk_artefacts\Release)
;
; Installs:
;   VST3        -> {commoncf64}\VST3\MasterDesk.vst3      (bundle folder)
;   CLAP        -> {commoncf64}\CLAP\MasterDesk.clap      (only if it was built)
;   Standalone  -> {app}\MasterDesk.exe                   (optional component)

#ifndef MyAppVersion
  #define MyAppVersion "1.0.0"
#endif
#ifndef BuildDir
  #define BuildDir "..\build\MasterDesk_artefacts\Release"
#endif

#define MyAppName      "MasterDesk"
#define MyAppPublisher "MasterDesk Audio"
#define ClapArtefact   BuildDir + "\CLAP\MasterDesk.clap"
#define HaveClap       FileExists(ClapArtefact)

[Setup]
AppId={{7B54D3A2-1C89-4E5B-9F1D-52AE30B4D5C1}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppVerName={#MyAppName} {#MyAppVersion}
AppPublisher={#MyAppPublisher}
DefaultDirName={autopf64}\{#MyAppName}
DefaultGroupName={#MyAppName}
DisableProgramGroupPage=yes
DisableWelcomePage=no
OutputDir=Output
OutputBaseFilename={#MyAppName}-{#MyAppVersion}-Windows-Setup
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
UninstallDisplayName={#MyAppName} {#MyAppVersion}
AppSupportURL=https://github.com/jtekkk/masterdesk
VersionInfoVersion={#MyAppVersion}

[Types]
Name: "full";    Description: "Full installation"
Name: "custom";  Description: "Custom installation"; Flags: iscustom

[Components]
Name: "vst3";       Description: "VST3 plug-in (64-bit)";      Types: full custom; Flags: fixed
#if HaveClap
Name: "clap";       Description: "CLAP plug-in (64-bit)";      Types: full custom
#endif
Name: "standalone"; Description: "Standalone application";     Types: full custom

[Files]
Source: "{#BuildDir}\VST3\MasterDesk.vst3\*"; DestDir: "{commoncf64}\VST3\MasterDesk.vst3"; \
    Flags: ignoreversion recursesubdirs createallsubdirs; Components: vst3
#if HaveClap
Source: "{#ClapArtefact}"; DestDir: "{commoncf64}\CLAP"; Flags: ignoreversion; Components: clap
#endif
Source: "{#BuildDir}\Standalone\MasterDesk.exe"; DestDir: "{app}"; \
    Flags: ignoreversion; Components: standalone
Source: "..\README.md"; DestDir: "{app}"; DestName: "README.md"; Flags: ignoreversion

[Icons]
Name: "{group}\{#MyAppName}";           Filename: "{app}\MasterDesk.exe"; Components: standalone
Name: "{group}\Uninstall {#MyAppName}"; Filename: "{uninstallexe}"

[Run]
Filename: "{app}\MasterDesk.exe"; Description: "Launch {#MyAppName} standalone"; \
    Flags: nowait postinstall skipifsilent unchecked; Components: standalone

[UninstallDelete]
; user presets are intentionally left in place ({userappdata}\MasterDesk)
Type: filesandordirs; Name: "{commoncf64}\VST3\MasterDesk.vst3"
