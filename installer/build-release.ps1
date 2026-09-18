# Packages the built plugin as a GitHub release asset and builds the online installer.
#
#   .\installer\build-release.ps1 -Repo "yourname/win-frame-filter"
#
# Produces in .\dist :
#   win-frame-filter-<ver>-win64.zip      upload this to the GitHub release  v<ver>
#   win-frame-filter-<ver>-win64.zip.sha256
#   update.json                            host this (e.g. raw.githubusercontent.com) for "Check for Updates"
#   WindowsCameraFrame-Setup-<ver>.exe     the online installer (downloads the zip above from GitHub)
param(
    [string]$Repo = "YOUR-GITHUB-USER/win-frame-filter",
    [string]$Dll = "$PSScriptRoot\..\build\RelWithDebInfo\win-frame-filter.dll",
    [string]$Iscc = "$env:LOCALAPPDATA\Programs\Inno Setup 6\ISCC.exe"
)

$ErrorActionPreference = "Stop"
$root = Resolve-Path "$PSScriptRoot\.."
$cmake = Get-Content "$root\CMakeLists.txt" -Raw
if ($cmake -notmatch 'project\(win-frame-filter VERSION ([0-9.]+)') { throw "Could not read the version from CMakeLists.txt" }
$ver = $Matches[1]

if (-not (Test-Path $Dll)) { throw "Plugin DLL not found: $Dll  (build it first, see README)" }

$dist = "$root\dist"
$stage = "$dist\stage"
Remove-Item $stage -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force "$stage\bin\64bit", "$stage\data" | Out-Null
Copy-Item $Dll "$stage\bin\64bit\"
Copy-Item "$root\data\*" "$stage\data\" -Recurse

$zip = "$dist\win-frame-filter-$ver-win64.zip"
Remove-Item $zip -ErrorAction SilentlyContinue
Compress-Archive -Path "$stage\*" -DestinationPath $zip
Remove-Item $stage -Recurse -Force

$hash = (Get-FileHash $zip -Algorithm SHA256).Hash.ToLower()
"$hash  $(Split-Path $zip -Leaf)" | Set-Content "$zip.sha256" -Encoding ascii
@{ version = $ver; url = "https://github.com/$Repo/releases/latest" } | ConvertTo-Json | Set-Content "$root\update.json" -Encoding ascii

if (-not (Test-Path $Iscc)) { throw "Inno Setup 6 not found at $Iscc (winget install JRSoftware.InnoSetup)" }
& $Iscc "/DAppVersion=$ver" "/DGitHubRepo=$Repo" "/DPluginSha256=$hash" "$PSScriptRoot\win-frame-filter.iss"
if ($LASTEXITCODE -ne 0) { throw "Inno Setup failed" }

Write-Host ""
Write-Host "Version      : $ver"
Write-Host "Zip          : $zip"
Write-Host "SHA-256      : $hash"
Write-Host "Installer    : $dist\WindowsCameraFrame-Setup-$ver.exe"
Write-Host "Upload the zip to https://github.com/$Repo/releases (tag v$ver), then ship the installer."
