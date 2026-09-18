; Online installer for the Windows Camera Frame OBS plugin (Inno Setup 6.1+).
;
; Nothing but the small installer is shipped in this file: everything else is
; downloaded from GitHub at install time and verified with SHA-256:
;   * OBS Studio 32.0.1 (only if OBS is not installed) - the exact version the
;     plugin is built against, from github.com/obsproject/obs-studio releases
;   * the plugin itself - a zip attached to a release of YOUR repository
;
; Build with installer\build-release.ps1 (it passes the defines below).

#ifndef AppVersion
  #define AppVersion "1.1.0"
#endif
#ifndef GitHubRepo
  #define GitHubRepo "YOUR-GITHUB-USER/win-frame-filter"
#endif
#ifndef PluginSha256
  #define PluginSha256 ""
#endif

#define ObsVersion "32.0.1"
#define ObsFile "OBS-Studio-32.0.1-Windows-x64-Installer.exe"
#define ObsUrl "https://github.com/obsproject/obs-studio/releases/download/32.0.1/OBS-Studio-32.0.1-Windows-x64-Installer.exe"
#define ObsSha256 "71b938e77e1bf48b3e46d17e7faff596f03c4a964213b2919211ec5ac0f8952b"
#define PluginZip "win-frame-filter-" + AppVersion + "-win64.zip"
#define PluginUrlDefault "https://github.com/" + GitHubRepo + "/releases/download/v" + AppVersion + "/" + PluginZip

[Setup]
AppId={{6F2A9C1E-4B7D-4E63-9A55-2C8D7B1F0E41}
AppName=Windows Camera Frame for OBS Studio
AppVersion={#AppVersion}
AppPublisher=Windows Camera Frame
AppPublisherURL=https://github.com/{#GitHubRepo}
DefaultDirName={commonappdata}\obs-studio\plugins\win-frame-filter
DisableDirPage=yes
DisableProgramGroupPage=yes
DisableReadyPage=no
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
OutputDir=..\dist
OutputBaseFilename=WindowsCameraFrame-Setup-{#AppVersion}
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
UninstallFilesDir={app}
UninstallDisplayName=Windows Camera Frame for OBS Studio
CloseApplications=no

[Messages]
WelcomeLabel2=This will install the Windows Camera Frame plugin for OBS Studio.%n%nThe plugin (and OBS Studio itself, if it is missing) is downloaded from GitHub and checked against a SHA-256 hash before anything is installed. An internet connection is required.%n%nClose OBS Studio before continuing.

[Run]
Filename: "{commonpf64}\obs-studio\bin\64bit\obs64.exe"; WorkingDir: "{commonpf64}\obs-studio\bin\64bit"; Description: "Start OBS Studio now"; Flags: postinstall nowait skipifsilent unchecked

[UninstallDelete]
Type: filesandordirs; Name: "{app}\bin"
Type: filesandordirs; Name: "{app}\data"

[Code]
var
  DownloadPage: TDownloadWizardPage;
  ObsInstallerDownloaded: Boolean;

function ObsExePath(): String;
var
  Dir: String;
begin
  Result := '';
  if RegQueryStringValue(HKLM64, 'SOFTWARE\OBS Studio', '', Dir) then
    if FileExists(Dir + '\bin\64bit\obs64.exe') then
      Result := Dir + '\bin\64bit\obs64.exe';
  if Result = '' then
    if FileExists(ExpandConstant('{commonpf64}\obs-studio\bin\64bit\obs64.exe')) then
      Result := ExpandConstant('{commonpf64}\obs-studio\bin\64bit\obs64.exe');
end;

function ObsRunning(): Boolean;
var
  Code: Integer;
begin
  Result := Exec(ExpandConstant('{cmd}'), '/C tasklist /FI "IMAGENAME eq obs64.exe" | find /I "obs64.exe"', '',
                 SW_HIDE, ewWaitUntilTerminated, Code) and (Code = 0);
end;

function OnDownloadProgress(const Url, FileName: String; const Progress, ProgressMax: Int64): Boolean;
begin
  if Progress = ProgressMax then
    Log(Format('Downloaded %s', [FileName]));
  Result := True;
end;

procedure InitializeWizard();
begin
  DownloadPage := CreateDownloadPage(SetupMessage(msgWizardPreparing), SetupMessage(msgPreparingDesc),
                                     @OnDownloadProgress);
end;

function PluginUrl(): String;
begin
  Result := ExpandConstant('{param:PluginUrl|{#PluginUrlDefault}}');
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
begin
  Result := '';

  if ObsRunning() then
  begin
    Result := 'OBS Studio is running. Please close it (the plugin file is locked while OBS is open) and run this installer again.';
    Exit;
  end;

  DownloadPage.Clear;
  ObsInstallerDownloaded := False;

  if ObsExePath() = '' then
  begin
    if WizardSilent or (MsgBox('OBS Studio was not found on this computer.' + #13#10#13#10 +
         'Download and install OBS Studio {#ObsVersion} (about 150 MB) from GitHub first? ' +
         'This version matches the plugin.', mbConfirmation, MB_YESNO) = IDYES) then
    begin
      DownloadPage.Add('{#ObsUrl}', '{#ObsFile}', '{#ObsSha256}');
      ObsInstallerDownloaded := True;
    end
    else
    begin
      Result := 'OBS Studio is required for this plugin.';
      Exit;
    end;
  end;

  DownloadPage.Add(PluginUrl(), 'plugin.zip', '{#PluginSha256}');

  DownloadPage.Show;
  try
    try
      DownloadPage.Download;
    except
      if DownloadPage.AbortedByUser then
        Result := 'The download was cancelled.'
      else
        Result := 'A download failed or did not match its expected checksum:' + #13#10 + GetExceptionMessage;
    end;
  finally
    DownloadPage.Hide;
  end;
end;

procedure CurStepChanged(CurStep: TSetupStep);
var
  Code: Integer;
  Zip, Cmd: String;
begin
  if CurStep <> ssPostInstall then
    Exit;

  { 1. OBS Studio itself, if it was missing (its installer asks for admin rights) }
  if ObsInstallerDownloaded then
  begin
    WizardForm.StatusLabel.Caption := 'Installing OBS Studio {#ObsVersion}...';
    if not ShellExec('runas', ExpandConstant('{tmp}\{#ObsFile}'), '/S', '', SW_SHOW, ewWaitUntilTerminated, Code) or
       (Code <> 0) then
      MsgBox('OBS Studio could not be installed automatically (code ' + IntToStr(Code) + '). ' +
             'Install it from obsproject.com, then run this installer again.', mbError, MB_OK);
  end;

  { 2. The plugin: unpack the verified zip into the OBS plugin folder }
  WizardForm.StatusLabel.Caption := 'Installing the plugin...';
  ForceDirectories(ExpandConstant('{app}'));
  Zip := ExpandConstant('{tmp}\plugin.zip');
  Cmd := '-NoProfile -ExecutionPolicy Bypass -Command "Expand-Archive -LiteralPath ''' + Zip +
         ''' -DestinationPath ''' + ExpandConstant('{app}') + ''' -Force"';
  if not Exec('powershell.exe', Cmd, '', SW_HIDE, ewWaitUntilTerminated, Code) or (Code <> 0) or
     not FileExists(ExpandConstant('{app}\bin\64bit\win-frame-filter.dll')) then
    MsgBox('The plugin files could not be unpacked. Nothing was changed in OBS.', mbError, MB_OK);
end;
