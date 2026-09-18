# Installs the built plugin into OBS Studio's per-user plugin folder
# (C:\ProgramData\obs-studio\plugins\win-frame-filter). No admin rights needed.
# Close OBS first: it keeps the DLL locked while running.
param(
    [string]$Dll = "$PSScriptRoot\build\RelWithDebInfo\win-frame-filter.dll"
)

if (Get-Process obs64 -ErrorAction SilentlyContinue) {
    Write-Error "OBS Studio is running - close it first (it locks the plugin DLL)."
    exit 1
}
if (-not (Test-Path $Dll)) {
    Write-Error "Plugin DLL not found: $Dll  (build it first, see README)"
    exit 1
}

$dst = "C:\ProgramData\obs-studio\plugins\win-frame-filter"
New-Item -ItemType Directory -Force -Path "$dst\bin\64bit", "$dst\data" | Out-Null
Copy-Item $Dll "$dst\bin\64bit\" -Force
Copy-Item "$PSScriptRoot\data\*" "$dst\data\" -Recurse -Force
Write-Host "Installed to $dst"
