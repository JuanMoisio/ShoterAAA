# Shared helpers for the ShoterAAA build scripts (dot-source this file).
$ErrorActionPreference = 'Stop'

$ProjectRoot = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$ProjectFile = Join-Path $ProjectRoot 'ShoterAAA.uproject'

function Find-UnrealEngine {
    param([string]$Version = '5.8')

    # 1) Epic Games Launcher install list
    $dat = 'C:\ProgramData\Epic\UnrealEngineLauncher\LauncherInstalled.dat'
    if (Test-Path $dat) {
        $list = (Get-Content $dat -Raw | ConvertFrom-Json).InstallationList
        $entry = $list | Where-Object { $_.AppName -eq "UE_$Version" } | Select-Object -First 1
        if ($entry -and (Test-Path $entry.InstallLocation)) { return $entry.InstallLocation }
    }

    # 2) Launcher manifests (newer launchers may not update LauncherInstalled.dat right away)
    $manifests = 'C:\ProgramData\Epic\EpicGamesLauncher\Data\Manifests'
    if (Test-Path $manifests) {
        foreach ($item in Get-ChildItem $manifests -Filter *.item) {
            $manifest = Get-Content $item.FullName -Raw | ConvertFrom-Json
            if ($manifest.AppName -eq "UE_$Version" -and -not $manifest.bIsIncompleteInstall -and (Test-Path $manifest.InstallLocation)) {
                return $manifest.InstallLocation
            }
        }
    }

    # 3) Registry written by the launcher
    try {
        $dir = (Get-ItemProperty "HKLM:\SOFTWARE\EpicGames\Unreal Engine\$Version" -ErrorAction Stop).InstalledDirectory
        if ($dir -and (Test-Path $dir)) { return $dir }
    } catch { }

    # 4) Usual folders
    foreach ($candidate in @("D:\Juegos epicos\UE_$Version", "D:\EPICAGAMES\Epic Games\UE_$Version", "C:\Program Files\Epic Games\UE_$Version")) {
        if (Test-Path (Join-Path $candidate 'Engine\Binaries\Win64\UnrealEditor.exe')) { return $candidate }
    }

    throw "No se encontró Unreal Engine $Version. Instalalo desde el Epic Games Launcher."
}
