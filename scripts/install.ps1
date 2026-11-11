# Installs spoticli.exe so `spoticli` works from any new terminal.
# Usage: make install   (or: powershell -File scripts\install.ps1 -Exe spoticli.exe)
param(
    [string]$Exe = "spoticli.exe",
    [switch]$Uninstall
)
$ErrorActionPreference = "Stop"

$dest = Join-Path $env:LOCALAPPDATA "Programs\spoticli"
$userPath = [Environment]::GetEnvironmentVariable("Path", "User")
$parts = @($userPath -split ";" | Where-Object { $_ -ne "" })
$onPath = $parts | Where-Object { $_.TrimEnd("\") -ieq $dest }

if ($Uninstall) {
    if (Test-Path $dest) { Remove-Item -Recurse -Force $dest }
    if ($onPath) {
        $newPath = ($parts | Where-Object { $_.TrimEnd("\") -ine $dest }) -join ";"
        [Environment]::SetEnvironmentVariable("Path", $newPath, "User")
    }
    Write-Host "Uninstalled spoticli. (Your saved credentials in $env:APPDATA\spoticli were left in place.)"
    exit 0
}

if (-not (Test-Path $Exe)) { throw "Build output not found: $Exe (run 'make' first)" }

New-Item -ItemType Directory -Force -Path $dest | Out-Null
try {
    Copy-Item -Force $Exe (Join-Path $dest "spoticli.exe")
} catch {
    throw "Could not replace $dest\spoticli.exe. Close any running spoticli and try again."
}

if (-not $onPath) {
    $newPath = (@($parts) + $dest) -join ";"
    [Environment]::SetEnvironmentVariable("Path", $newPath, "User")
    Write-Host "Added $dest to your user PATH."
    Write-Host "Open a NEW terminal, then type: spoticli"
} else {
    Write-Host "Updated $dest\spoticli.exe. Type 'spoticli' in any terminal."
}
