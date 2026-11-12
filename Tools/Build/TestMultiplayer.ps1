# Headless multiplayer smoke test: a dedicated server with bots plus one client, both without rendering.
# Checks the logs for a successful join, kills, respawns and crashes.
# Usage: powershell -ExecutionPolicy Bypass -File Tools\Build\TestMultiplayer.ps1 [-Mode TDM|FFA|Zombies] [-Bots 4] [-Seconds 90]
param(
    [ValidateSet('TDM', 'FFA', 'Zombies')]
    [string]$Mode = 'TDM',
    [int]$Bots = 4,
    [int]$Seconds = 90,
    [int]$Port = 7787
)
. "$PSScriptRoot\ShoterCommon.ps1"

$engine = Find-UnrealEngine
$editor = Join-Path $engine 'Engine\Binaries\Win64\UnrealEditor.exe'
$logDir = Join-Path $ProjectRoot 'Saved\Logs\SmokeTest'
New-Item -ItemType Directory -Force $logDir | Out-Null
$serverLog = Join-Path $logDir "Server_$Mode.log"
$clientLog = Join-Path $logDir "Client_$Mode.log"
Remove-Item $serverLog, $clientLog -ErrorAction SilentlyContinue

$common = '-nosteam -nullrhi -nosound -unattended -nosplash -NoVerifyGC -log'
# Braces matter: in PowerShell '?' is a valid variable-name character ("$Mode?Bots" would be one variable).
$map = "/Game/Shoter/Maps/Arena_Neon?game=${Mode}?Bots=${Bots}"

Write-Host "Servidor: $map (puerto $Port)"
$server = Start-Process $editor -PassThru -WindowStyle Minimized -ArgumentList "`"$ProjectFile`" $map -server -port=$Port $common -abslog=`"$serverLog`""

# Wait until the server is listening (or 120 s).
$deadline = (Get-Date).AddSeconds(120)
while ((Get-Date) -lt $deadline -and -not $server.HasExited) {
    if ((Test-Path $serverLog) -and (Select-String -Path $serverLog -Pattern 'listening on port|InitListen|GameNetDriver' -Quiet)) { break }
    Start-Sleep -Seconds 2
}

Write-Host "Cliente conectándose a 127.0.0.1:$Port"
$client = Start-Process $editor -PassThru -WindowStyle Minimized -ArgumentList "`"$ProjectFile`" 127.0.0.1:$Port -game $common -abslog=`"$clientLog`""

Start-Sleep -Seconds $Seconds

foreach ($process in @($client, $server)) {
    if ($process -and -not $process.HasExited) { Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue }
}
Start-Sleep -Seconds 2

function Count-Matches($path, $pattern) {
    if (-not (Test-Path $path)) { return 0 }
    # Ignore the engine's own startup self-test lines (UnifiedErrorTest / "FError that has been ...").
    return @(Select-String -Path $path -Pattern $pattern | Where-Object { $_.Line -notmatch 'UnifiedErrorTest|FError that has been' }).Count
}

$report = [ordered]@{
    'Cliente conectado (Join succeeded)' = Count-Matches $clientLog 'Join succeeded|Welcomed by server'
    'Jugadores que entraron al servidor' = Count-Matches $serverLog 'LogShoter.*joined'
    'Bajas registradas'                  = Count-Matches $serverLog 'LogShoter.*Kill:'
    'Oleadas iniciadas (zombies)'        = Count-Matches $serverLog 'LogShoter.*wave .* starts'
    'Errores del servidor'               = Count-Matches $serverLog 'Error:'
    'Errores del cliente'                = Count-Matches $clientLog 'Error:'
    'Crashes / asserts'                  = (Count-Matches $serverLog 'Assertion failed|Fatal error|Unhandled Exception') + (Count-Matches $clientLog 'Assertion failed|Fatal error|Unhandled Exception')
}
$report.GetEnumerator() | ForEach-Object { "{0,-40} {1}" -f $_.Key, $_.Value }
Write-Host "Logs: $logDir"
