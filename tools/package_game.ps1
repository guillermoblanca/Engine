# Packages the game (GB.exe) into a folder that runs on its own: D:/gb-release/GB_<date>/ by default.
#   GB.exe, GB.map (the crash trace's offsets resolve against it), assets/ and packs/full (the X Bot look of the clans; without it they are CesiumMan).
#   GB.exe looks for assets/ beside itself first (GameMain.cpp), so the folder can be moved or renamed; saves/ and the session folders are made
#   beside the exe on the first game.
# Usage (from the repository root, after a Release build):
#   powershell -ExecutionPolicy Bypass -File tools/package_game.ps1 [-Build build] [-Out D:/gb-release] [-Name GB_2026-11-15] [-NoPacks] [-Zip]
param(
    [string]$Build = "build",
    [string]$Out = "D:/gb-release",
    [string]$Name = "",
    [switch]$NoPacks,
    [switch]$Zip
)
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
if (-not [System.IO.Path]::IsPathRooted($Build)) { $Build = Join-Path $root $Build }
$bin = Join-Path $Build "Editor/Release"
$exe = Join-Path $bin "GB.exe"
if (-not (Test-Path $exe)) { throw "No GB.exe in ${bin}: build first (cmake --build $Build --config Release --target GBGame)" }
if ($Name -eq "") { $Name = "GB_" + (Get-Date -Format "yyyy-MM-dd") }
$dest = Join-Path $Out $Name
if (Test-Path $dest) { throw "$dest already exists: remove it or pass -Name" }
New-Item -ItemType Directory -Force -Path $dest | Out-Null

Copy-Item $exe $dest
$map = Join-Path $bin "GB.map"
if (Test-Path $map) { Copy-Item $map $dest }
Get-ChildItem $bin -Filter *.dll -ErrorAction SilentlyContinue | ForEach-Object { Copy-Item $_.FullName $dest }   # none today: the build is static

# robocopy: exit codes below 8 are success
robocopy (Join-Path $root "assets") (Join-Path $dest "assets") /E /NFL /NDL /NJH /NJS /NP /XD __pycache__ | Out-Null
if ($LASTEXITCODE -ge 8) { throw "robocopy assets failed ($LASTEXITCODE)" }
$packs = Join-Path $root "packs/full"
if (-not $NoPacks -and (Test-Path $packs)) {
    robocopy $packs (Join-Path $dest "packs/full") /E /NFL /NDL /NJH /NJS /NP | Out-Null
    if ($LASTEXITCODE -ge 8) { throw "robocopy packs failed ($LASTEXITCODE)" }
}

$hash = (git -C $root describe --always --dirty 2>$null)
@"
GB - $Name
Version: $hash, packaged $(Get-Date -Format "yyyy-MM-dd HH:mm")

Double click GB.exe. The start menu: new game (god mode), load game, settings, quit.
In a game: Esc pause menu, F5 quick save, F9 quick load, F8 director's note, F12 capture.
Every game writes its session folder in saves/sessions/<date_time>_<biome>_<seed>/: game.log, autosaves,
telemetry.db, notes.md with captures, shots/ and session.json. Send that folder to report what you saw.
GB.exe --windowed opens in a window; GB.exe --quality=low|medium|high chooses the quality.
"@ | Set-Content -Encoding utf8 (Join-Path $dest "README.txt")

$size = (Get-ChildItem $dest -Recurse | Measure-Object -Property Length -Sum).Sum / 1MB
Write-Host ("Packaged {0} ({1:N0} MB, version {2})" -f $dest, $size, $hash)
if ($Zip) {
    $zipPath = "$dest.zip"
    Compress-Archive -Path $dest -DestinationPath $zipPath -Force
    Write-Host "Zip: $zipPath"
}
