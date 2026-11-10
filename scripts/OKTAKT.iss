; OKTAKT installer for Windows (Inno Setup 6.5.2 or newer).
; Built by scripts\build-windows-installer.bat, which passes /DAppVersion=<version from CMakeLists.txt>.
; Tick what to install: the app (Standalone), VST3, CLAP and the demo kit.
; Uninstall: Windows Settings > Apps > OKTAKT (or Control Panel > Programs).

#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif
#define Dist AddBackslash(SourcePath) + "..\dist-windows"
#define Res  AddBackslash(SourcePath) + "..\resources"
#define HasClap FileExists(Dist + "\OKTAKT.clap")

[Setup]
; Keep AppId forever: Windows uses it to see that OKTAKT is already installed
AppId={{6876C7D0-AA3F-409F-8651-2E5648CA5602}
AppName=OKTAKT
AppVersion={#AppVersion}
AppVerName=OKTAKT {#AppVersion}
AppPublisher=OKTAKT
VersionInfoVersion={#AppVersion}
DefaultDirName={autopf}\OKTAKT
DisableProgramGroupPage=yes
PrivilegesRequired=admin
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
WizardStyle=modern
SetupIconFile={#Res}\icon\OKTAKT.ico
WizardSmallImageFile={#Res}\icon\wizard-small.png,{#Res}\icon\wizard-small-2x.png
UninstallDisplayName=OKTAKT
UninstallDisplayIcon={app}\OKTAKT.exe
CloseApplications=yes
RestartApplications=no
OutputDir={#Dist}
OutputBaseFilename=OKTAKT-{#AppVersion}-Windows-Setup
Compression=lzma2
SolidCompression=yes

[Types]
Name: "full";   Description: "Everything"
Name: "custom"; Description: "Choose what to install"; Flags: iscustom

[Components]
Name: "app";  Description: "OKTAKT app (Standalone)";  Types: full custom
Name: "vst3"; Description: "VST3 plug-in (Common Files\VST3)"; Types: full custom
#if HasClap
Name: "clap"; Description: "CLAP plug-in (Common Files\CLAP)"; Types: full custom
#endif
Name: "demo"; Description: "Demo kit: drums, breaks and loops (3.5 MB)"; Types: full custom

[Files]
Source: "{#Dist}\OKTAKT.exe"; DestDir: "{app}"; Components: app; Flags: ignoreversion
Source: "{#Dist}\OKTAKT.vst3\*"; DestDir: "{commoncf64}\VST3\OKTAKT.vst3"; Components: vst3; Flags: ignoreversion recursesubdirs createallsubdirs
#if HasClap
Source: "{#Dist}\OKTAKT.clap"; DestDir: "{commoncf64}\CLAP"; Components: clap; Flags: ignoreversion
#endif
; The app opens on it the first time (C:\ProgramData\OKTAKT\Demo Kit)
Source: "{#Res}\demo-kit\*"; DestDir: "{commonappdata}\OKTAKT\Demo Kit"; Components: demo; Excludes: ".DS_Store"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{autoprograms}\OKTAKT"; Filename: "{app}\OKTAKT.exe"; Components: app

[Code]
function IsOktaktRunning(): Boolean;
var
  Locator, Service, Found: Variant;
begin
  Result := False;
  try
    Locator := CreateOleObject('WbemScripting.SWbemLocator');
    Service := Locator.ConnectServer('.', 'root\CIMV2');
    Found := Service.ExecQuery('SELECT ProcessId FROM Win32_Process WHERE Name = ''OKTAKT.exe''');
    Result := Found.Count > 0;
  except
  end;
end;

function InitializeSetup(): Boolean;
begin
  Result := True;
  while Result and IsOktaktRunning() do
    Result := SuppressibleMsgBox('The OKTAKT app is open. Quit it, then click Retry.', mbError, MB_RETRYCANCEL, IDCANCEL) = IDRETRY;
end;

function InitializeUninstall(): Boolean;
begin
  Result := True;
  while Result and IsOktaktRunning() do
    Result := SuppressibleMsgBox('The OKTAKT app is open. Quit it, then click Retry.', mbError, MB_RETRYCANCEL, IDCANCEL) = IDRETRY;
end;

// Settings are kept unless you say so; saved patterns in Documents\OKTAKT are never touched
procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
var
  Dir: String;
begin
  if CurUninstallStep <> usPostUninstall then Exit;
  Dir := ExpandConstant('{userappdata}\OKTAKT');
  if DirExists(Dir) and (SuppressibleMsgBox('Also remove the OKTAKT app settings (audio device and others)?' + #13#10 +
      'Saved patterns in Documents\OKTAKT stay either way.', mbConfirmation, MB_YESNO or MB_DEFBUTTON2, IDNO) = IDYES) then
    DelTree(Dir, True, True, True);
end;
