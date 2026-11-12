# Runs the game (editor build, -game) with -ShoterAutoShots: it takes 6 screenshots doing a few actions, then quits.
# Screenshots are copied to Saved\Screenshots\Shoter_<Name>\.
# Usage: powershell -ExecutionPolicy Bypass -File Tools\Build\Screenshots.ps1 [-Map Arena_Neon|MainMenu] [-Mode TDM|FFA|Zombies] [-Bots 5]
param(
    [string]$Map = 'Arena_Neon',
    [ValidateSet('TDM', 'FFA', 'Zombies', 'Menu')]
    [string]$Mode = 'TDM',
    [int]$Bots = 5,
    [int]$Width = 1600,
    [int]$Height = 900,
    [int]$TimeoutSeconds = 240,
    # Extra dev switches, e.g. '-ShoterShotWeapon=Scatter -ShoterShotFireAds'
    [string]$Extra = ''
)
. "$PSScriptRoot\ShoterCommon.ps1"

$engine = Find-UnrealEngine
$editor = Join-Path $engine 'Engine\Binaries\Win64\UnrealEditor.exe'
$shotRoot = Join-Path $ProjectRoot 'Saved\Screenshots'
$name = if ($Mode -eq 'Menu') { 'Menu' } else { "${Map}_$Mode" }
$outDir = Join-Path $shotRoot "Shoter_$name"
New-Item -ItemType Directory -Force $outDir | Out-Null
Get-ChildItem $outDir -Filter *.png -ErrorAction SilentlyContinue | Remove-Item -Force

$started = Get-Date
$url = if ($Mode -eq 'Menu') { "/Game/Shoter/Maps/MainMenu" } else { "/Game/Shoter/Maps/$Map`?game=$Mode`?Bots=$Bots" }
$log = Join-Path $ProjectRoot "Saved\Logs\Screenshots_$name.log"
$args = "`"$ProjectFile`" $url -game -windowed -resx=$Width -resy=$Height -nosteam -ShoterAutoShots -NoVerifyGC $Extra -log -abslog=`"$log`""
Write-Host "Lanzando: $url"
$process = Start-Process $editor -ArgumentList $args -PassThru
if (-not $process.WaitForExit($TimeoutSeconds * 1000)) {
    Write-Host "Timeout: cerrando el juego." -ForegroundColor Yellow
    Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue
}

# HighResShot writes into Saved\Screenshots\<platform>\; collect the new ones.
$shots = Get-ChildItem $shotRoot -Recurse -Filter *.png | Where-Object { $_.LastWriteTime -ge $started -and $_.DirectoryName -ne $outDir } | Sort-Object LastWriteTime
$index = 0
foreach ($shot in $shots) {
    $index++
    Copy-Item $shot.FullName (Join-Path $outDir ("shot_{0:D2}.png" -f $index)) -Force
}
Write-Host "$index capturas en $outDir"
