# Compiles the editor build of the game (what the Unreal editor loads).
# Usage: powershell -ExecutionPolicy Bypass -File Tools\Build\Compile.ps1 [-Configuration Development|DebugGame]
param(
    [ValidateSet('Development', 'DebugGame')]
    [string]$Configuration = 'Development'
)
. "$PSScriptRoot\ShoterCommon.ps1"

$engine = Find-UnrealEngine
$buildBat = Join-Path $engine 'Engine\Build\BatchFiles\Build.bat'
Write-Host "Engine: $engine"
Write-Host "Compilando ShoterAAAEditor ($Configuration)..."

& $buildBat ShoterAAAEditor Win64 $Configuration "-Project=$ProjectFile" -WaitMutex -NoHotReloadFromIDE
exit $LASTEXITCODE
