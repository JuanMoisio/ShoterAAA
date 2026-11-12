# Builds, cooks and packages the game for Windows, then zips it so it can be shared with friends.
# Output: Build\Windows\ (playable folder) and Build\ShoterAAA_Windows.zip
# Usage: powershell -ExecutionPolicy Bypass -File Tools\Build\Package.ps1 [-Configuration Development|Shipping]
param(
    [ValidateSet('Development', 'Shipping')]
    [string]$Configuration = 'Development'
)
. "$PSScriptRoot\ShoterCommon.ps1"

$engine = Find-UnrealEngine
$uat = Join-Path $engine 'Engine\Build\BatchFiles\RunUAT.bat'
$archiveDir = Join-Path $ProjectRoot 'Build'
Write-Host "Engine: $engine"
Write-Host "Empaquetando ($Configuration) en $archiveDir ..."

& $uat BuildCookRun "-project=$ProjectFile" -noP4 -platform=Win64 "-clientconfig=$Configuration" `
    -build -cook -stage -pak -iostore -compressed -prereqs -archive "-archivedirectory=$archiveDir" -utf8output
if ($LASTEXITCODE -ne 0) {
    Write-Host "El empaquetado falló (código $LASTEXITCODE)." -ForegroundColor Red
    exit $LASTEXITCODE
}

$gameDir = Join-Path $archiveDir 'Windows'
if (-not (Test-Path $gameDir)) {
    throw "No se encontró la carpeta empaquetada: $gameDir"
}

# Steam test AppID (Spacewar, 480) next to both executables, so Steam works when launched outside Steam.
Set-Content -Path (Join-Path $gameDir 'steam_appid.txt') -Value '480' -NoNewline -Encoding ascii
$binDir = Join-Path $gameDir 'ShoterAAA\Binaries\Win64'
if (Test-Path $binDir) {
    Set-Content -Path (Join-Path $binDir 'steam_appid.txt') -Value '480' -NoNewline -Encoding ascii
}

# Shortcut to play without Steam (LAN or direct IP: everyone must use this mode).
$noSteam = "@echo off`r`nstart `"`" `"%~dp0ShoterAAA.exe`" -nosteam`r`n"
Set-Content -Path (Join-Path $gameDir 'Jugar sin Steam (LAN o IP).bat') -Value $noSteam -NoNewline -Encoding ascii

# Zip (Zip64 capable, no 2 GB limit).
$zipPath = Join-Path $archiveDir 'ShoterAAA_Windows.zip'
if (Test-Path $zipPath) { Remove-Item $zipPath -Force }
Add-Type -AssemblyName System.IO.Compression.FileSystem
# Playing from Build\Windows leaves local data in ShoterAAA\Saved (logs with machine paths, settings, screenshots).
# That must never go into the shared zip: move it aside while zipping, then put it back.
$savedDir = Join-Path $gameDir 'ShoterAAA\Saved'
$savedAside = Join-Path $archiveDir '_Saved_local'
if (Test-Path $savedAside) { throw "Quedó una carpeta temporal de un empaquetado anterior: $savedAside. Movela de vuelta a $savedDir o borrala." }
$movedSaved = $false
if (Test-Path $savedDir) {
    Move-Item $savedDir $savedAside
    $movedSaved = $true
}
try {
    [System.IO.Compression.ZipFile]::CreateFromDirectory($gameDir, $zipPath, [System.IO.Compression.CompressionLevel]::Optimal, $false)
}
finally {
    if ($movedSaved) { Move-Item $savedAside $savedDir }
}

$sizeMb = [math]::Round((Get-Item $zipPath).Length / 1MB)
Write-Host "Listo: $zipPath ($sizeMb MB)" -ForegroundColor Green
