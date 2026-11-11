<#
.SYNOPSIS
    Runs the ShoterAAA content build (audio import, materials, maps) in a headless Unreal Editor 5.8.

.DESCRIPTION
    Finds UnrealEditor-Cmd.exe (Epic launcher manifest, then D:\EPICAGAMES\Epic Games\UE_5.8, then
    C:\Program Files\Epic Games\UE_5.8) and runs

      UnrealEditor-Cmd.exe "D:\ShoterAAA\ShoterAAA.uproject" -run=pythonscript
          -script="D:\ShoterAAA\Tools\Editor\shoter_build_content.py" -unattended -nop4 -nosplash -NoSound

    The console output is shown and also written (UTF-8) to D:\ShoterAAA\Saved\Logs\ShoterBuild.log.
    The script exits with the editor's exit code (non-zero when a build step failed).

    The C++ module must be compiled first: the maps use AShoterPlayerStart, AShoterBuyStation, ...
    Use -Build to compile it (UnrealBuildTool, ShoterAAAEditor Win64 Development) before the content build.

.PARAMETER EngineRoot
    Folder of the UE 5.8 install (the one that contains Engine\). Skips the auto detection.

.PARAMETER Steps
    Comma separated subset of: audio, materials, maps.  Default: all three.

.PARAMETER ForceReimportAudio
    Re-import wav files even when the sound asset already exists.

.PARAMETER Build
    Compile the editor target of the project before running the content build.

.PARAMETER ExtraArgs
    Extra arguments appended to the UnrealEditor-Cmd.exe command line, e.g. -ExtraArgs '-stdout','-FullStdOutLogOutput'.

.EXAMPLE
    .\run_build_content.ps1
    .\run_build_content.ps1 -Steps maps
    .\run_build_content.ps1 -Build -ForceReimportAudio
#>
[CmdletBinding()]
param(
    [string]$EngineRoot = "",
    [string]$Steps = "",
    [switch]$ForceReimportAudio,
    [switch]$Build,
    [string[]]$ExtraArgs = @()
)

$ErrorActionPreference = "Stop"

$ProjectDir  = "D:\ShoterAAA"
$ProjectFile = Join-Path $ProjectDir "ShoterAAA.uproject"
$ScriptFile  = Join-Path $ProjectDir "Tools\Editor\shoter_build_content.py"
$LogFile     = Join-Path $ProjectDir "Saved\Logs\ShoterBuild.log"
$LauncherManifest = "C:\ProgramData\Epic\UnrealEngineLauncher\LauncherInstalled.dat"

function Find-EngineRoot {
    $candidates = New-Object System.Collections.Generic.List[string]
    if ($EngineRoot) { $candidates.Add($EngineRoot) }
    if ($env:UE_ROOT) { $candidates.Add($env:UE_ROOT) }

    # 1) Epic Games Launcher manifest: InstallationList[].{AppName = "UE_5.8", InstallLocation}
    if (Test-Path -LiteralPath $LauncherManifest) {
        try {
            $manifest = Get-Content -Raw -LiteralPath $LauncherManifest | ConvertFrom-Json
            foreach ($item in @($manifest.InstallationList)) {
                if ($item -and $item.AppName -match '^UE_5\.8' -and $item.InstallLocation) {
                    $candidates.Add([string]$item.InstallLocation)
                }
            }
        } catch {
            Write-Warning "Could not read $LauncherManifest : $($_.Exception.Message)"
        }
    }

    # 2) and 3) well known locations
    $candidates.Add("D:\EPICAGAMES\Epic Games\UE_5.8")
    $candidates.Add("C:\Program Files\Epic Games\UE_5.8")

    foreach ($root in $candidates) {
        if ($root -and (Test-Path -LiteralPath (Join-Path $root "Engine\Binaries\Win64\UnrealEditor-Cmd.exe"))) {
            return $root
        }
    }
    return $null
}

function Format-Arg([string]$Name, [string]$Value) {
    # Windows PowerShell 5.1 drops embedded quotes, so only quote values that really need it.
    if ($Value -match '\s') { return "$Name=`"$Value`"" }
    return "$Name=$Value"
}

# ------------------------------------------------------------------------------------------------ checks
if (-not (Test-Path -LiteralPath $ProjectFile)) { throw "Project file not found: $ProjectFile" }
if (-not (Test-Path -LiteralPath $ScriptFile))  { throw "Build script not found: $ScriptFile" }

$engine = Find-EngineRoot
if (-not $engine) {
    Write-Host ("ERROR: UnrealEditor-Cmd.exe not found. Looked at the Epic launcher manifest ($LauncherManifest), " +
                "D:\EPICAGAMES\Epic Games\UE_5.8 and C:\Program Files\Epic Games\UE_5.8. " +
                "Pass -EngineRoot <folder containing Engine\> or set the UE_ROOT environment variable.") -ForegroundColor Red
    exit 2
}
$editorCmd = Join-Path $engine "Engine\Binaries\Win64\UnrealEditor-Cmd.exe"

New-Item -ItemType Directory -Force -Path (Split-Path -Parent $LogFile) | Out-Null

# ------------------------------------------------------------------------------------------------ optional compile
if ($Build) {
    $buildBat = Join-Path $engine "Engine\Build\BatchFiles\Build.bat"
    Write-Host "Compiling ShoterAAAEditor (Win64 Development) with $buildBat ..."
    & $buildBat ShoterAAAEditor Win64 Development "-Project=$ProjectFile" -WaitMutex
    if ($LASTEXITCODE -ne 0) {
        Write-Host "ERROR: compilation failed with exit code $LASTEXITCODE; not running the content build." -ForegroundColor Red
        exit $LASTEXITCODE
    }
}

$moduleDll = Join-Path $ProjectDir "Binaries\Win64\UnrealEditor-ShoterAAA.dll"
if (-not (Test-Path -LiteralPath $moduleDll)) {
    Write-Warning ("The game module is not compiled ($moduleDll is missing). The maps need the C++ classes " +
                   "(ShoterPlayerStart, ShoterBuyStation, ShoterGameMode_TDM, ...). Re-run with -Build or compile the project first.")
}

# ------------------------------------------------------------------------------------------------ run
# Always (re)set the switches so values from an earlier run in the same PowerShell session cannot leak in.
if ($Steps)              { $env:SHOTER_STEPS = $Steps } else { Remove-Item Env:\SHOTER_STEPS -ErrorAction SilentlyContinue }
if ($ForceReimportAudio) { $env:SHOTER_FORCE_REIMPORT_AUDIO = "1" } else { Remove-Item Env:\SHOTER_FORCE_REIMPORT_AUDIO -ErrorAction SilentlyContinue }

$ueArgs = @(
    $ProjectFile,
    "-run=pythonscript",
    (Format-Arg "-script" $ScriptFile),
    "-unattended",
    "-nop4",
    "-nosplash",
    "-NoSound"
) + $ExtraArgs

try { [Console]::OutputEncoding = [System.Text.Encoding]::UTF8 } catch { }

$utf8NoBom = New-Object System.Text.UTF8Encoding($false)
$writer = New-Object System.IO.StreamWriter($LogFile, $false, $utf8NoBom)
$writer.AutoFlush = $true
$header = @(
    "ShoterBuild started $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')",
    "Engine : $engine",
    "Command: `"$editorCmd`" $($ueArgs -join ' ')",
    ("-" * 100)
)
foreach ($line in $header) { Write-Host $line; $writer.WriteLine($line) }

$exitCode = 1
$previousPreference = $ErrorActionPreference
$ErrorActionPreference = "Continue"      # native stderr lines (2>&1) must not abort the script in Windows PowerShell 5.1
try {
    & $editorCmd @ueArgs 2>&1 | ForEach-Object {
        $line = "$_"
        [Console]::WriteLine($line)
        $writer.WriteLine($line)
    }
    $exitCode = $LASTEXITCODE
} finally {
    $ErrorActionPreference = $previousPreference
    $writer.WriteLine(("-" * 100))
    $writer.WriteLine("UnrealEditor-Cmd exit code: $exitCode")
    $writer.Dispose()
}

# ------------------------------------------------------------------------------------------------ report
$errorLines   = @(Select-String -LiteralPath $LogFile -Pattern 'LogPython: Error: \[ShoterBuild\]').Count
$warningLines = @(Select-String -LiteralPath $LogFile -Pattern 'LogPython: Warning: \[ShoterBuild\]').Count
Write-Host ""
Write-Host "UnrealEditor-Cmd exit code: $exitCode  |  [ShoterBuild] error lines: $errorLines, warning lines: $warningLines"
Write-Host "Console log : $LogFile"
Write-Host "Editor log  : $(Join-Path $ProjectDir 'Saved\Logs\ShoterAAA.log')"
exit $exitCode
