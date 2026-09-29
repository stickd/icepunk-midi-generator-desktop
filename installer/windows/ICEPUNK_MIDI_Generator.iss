#ifndef AppVersion
  #define AppVersion "1.0.0"
#endif
#ifndef SourceDir
  #define SourceDir "..\..\dist\windows\ICEPUNK MIDI Generator"
#endif
#ifndef OutputDir
  #define OutputDir "..\..\dist"
#endif
#ifndef WithVST3
  #define WithVST3 0
#endif

[Setup]
AppId={{C6043015-6275-4E31-B695-576491B81E06}
AppName=ICEPUNK MIDI Generator
AppVersion={#AppVersion}
AppPublisher=ICEPUNK
AppVerName=ICEPUNK MIDI Generator {#AppVersion}
DefaultGroupName=ICEPUNK MIDI Generator
#if WithVST3
DefaultDirName={autopf}\ICEPUNK MIDI Generator
PrivilegesRequired=admin
#else
DefaultDirName={localappdata}\Programs\ICEPUNK MIDI Generator
PrivilegesRequired=lowest
#endif
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0.17763
OutputDir={#OutputDir}
OutputBaseFilename=ICEPUNK_MIDI_Generator_{#AppVersion}_Windows_Setup
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
UninstallDisplayIcon={app}\ICEPUNK.exe
DisableProgramGroupPage=yes
CloseApplications=yes
RestartApplications=no
SetupLogging=yes
#ifdef SignInstaller
SignTool=icepunk
SignedUninstaller=yes
#endif

[Tasks]
Name: "desktopicon"; Description: "Create a desktop shortcut"; GroupDescription: "Shortcuts:"; Flags: unchecked

[Files]
Source: "{#SourceDir}\ICEPUNK.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#SourceDir}\engine\*"; DestDir: "{app}\engine"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#SourceDir}\licenses\*"; DestDir: "{app}\licenses"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#SourceDir}\VERSION"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#SourceDir}\BUILD_INFO.json"; DestDir: "{app}"; Flags: ignoreversion
#if WithVST3
Source: "{#SourceDir}\vst3\*"; DestDir: "{commoncf64}\VST3\ICEPUNK.vst3"; Flags: ignoreversion recursesubdirs createallsubdirs
#endif

[Icons]
Name: "{group}\ICEPUNK MIDI Generator"; Filename: "{app}\ICEPUNK.exe"; WorkingDir: "{app}"
Name: "{autodesktop}\ICEPUNK MIDI Generator"; Filename: "{app}\ICEPUNK.exe"; WorkingDir: "{app}"; Tasks: desktopicon

[Run]
Filename: "{app}\ICEPUNK.exe"; Description: "Launch ICEPUNK MIDI Generator"; Flags: nowait postinstall skipifsilent

; Runtime data is created by the user process, never by an elevated installer.
; No UninstallDelete rule: user MIDI/settings/logs outside {app} are preserved.
