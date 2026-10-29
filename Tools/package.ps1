# =============================================================================
# package.ps1 - DesktopForces Windows Builder & Packager
#
# Usage:
#   powershell -ExecutionPolicy Bypass -File Tools\package.ps1
#   powershell -ExecutionPolicy Bypass -File Tools\package.ps1 -Config Development
#   powershell -ExecutionPolicy Bypass -File Tools\package.ps1 -Package:$false   (compile only)
#   powershell -ExecutionPolicy Bypass -File Tools\package.ps1 -Clean
#   powershell -ExecutionPolicy Bypass -File Tools\package.ps1 -Public   (no Classic / Unverified content)
#   powershell -ExecutionPolicy Bypass -File Tools\package.ps1 -Zip      (also one .zip to hand around)
#   powershell -ExecutionPolicy Bypass -File Tools\package.ps1 -Downloads
#       the core pack only; community / unverified paks + manifest for the download server in
#       Build\Windows-downloads\Downloads, the classic pak in ...\Sideload (never upload that one)
#   pwsh Tools/package.ps1 -DryRun   (print what would run; works on Linux too)
#
# Same build as Tools/package.sh on Linux: every map passed by name (the cooker doesn't take maps
# from the always-cook folders), iostore + compressed paks.
#
# The engine is the project's EngineAssociation (DesktopForces.uproject, "5.8"): -UePath or
# $env:UE_ROOT if given, else the same config and engine discovery as Tools/play.py.
# =============================================================================

param (
    [ValidateSet("Development", "Shipping")]
    [string]$Config = "Shipping",
    [switch]$Package = $true,                 # Full Cook & Package (default true; -Package:$false compiles only)
    [switch]$Clean = $false,                  # Clean Intermediate & Binaries before build
    [switch]$Public = $false,                 # Leave out the Classic and Unverified content plugins
    [switch]$DryRun = $false,                 # Print the commands instead of running them
    [switch]$Zip = $false,                    # Pack the finished build into one .zip next to it
    [switch]$Downloads = $false,              # Packs other than core as in-game downloads (Docs/Building.md)
    [string]$UePath = $env:UE_ROOT            # Explicit Unreal Engine path (optional)
)

$ErrorActionPreference = "Stop"
$ProjectDir = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$Uproject = Join-Path $ProjectDir "DesktopForces.uproject"
$OnWindows = $env:OS -eq "Windows_NT"         # ($IsWindows doesn't exist in Windows PowerShell 5.1)
$Assoc = (Get-Content $Uproject -Raw | ConvertFrom-Json).EngineAssociation

Write-Host "==========================================================" -ForegroundColor Cyan
Write-Host " DesktopForces Windows Builder" -ForegroundColor Cyan
Write-Host " Project: $Uproject" -ForegroundColor Cyan
Write-Host " Configuration: $Config$(if ($Public) { ' (public)' })$(if ($DryRun) { ' - dry run' })" -ForegroundColor Cyan
Write-Host "==========================================================" -ForegroundColor Cyan

# -----------------------------------------------------------------------------
# 1. Locate Unreal Engine
# -----------------------------------------------------------------------------
# Reuse the Python launcher's config/environment/registry discovery.
if (-not $UePath -and $OnWindows) {
    $Python = Get-Command python -ErrorAction SilentlyContinue
    $PythonArgs = @()
    if (-not $Python) {
        $Python = Get-Command py -ErrorAction SilentlyContinue
        $PythonArgs = @("-3")
    }
    if (-not $Python) { throw "Python 3.11+ is required; or supply -UePath explicitly." }
    $ResolvedEngine = & $Python.Source @PythonArgs (Join-Path $PSScriptRoot "play.py") --engine-root
    if ($LASTEXITCODE -ne 0) { throw "Engine discovery failed; check mfport.toml or supply -UePath." }
    $UePath = ($ResolvedEngine | Select-Object -Last 1).Trim()
}
if ($UePath) {
    if (-not (Test-Path (Join-Path $UePath "Engine/Build/BatchFiles/RunUAT.bat"))) {
        throw "No Windows engine at '$UePath'. Install it with Epic Games Launcher or set -UePath."
    }
    $FoundUe = (Resolve-Path $UePath).Path
} elseif ($DryRun) {
    $FoundUe = "<UE_$Assoc>"
} else {
    throw "Run Windows builds on Windows. Use -DryRun to inspect the commands from Linux."
}

Write-Host "[+] Unreal Engine: $FoundUe" -ForegroundColor Green
$RunUAT = Join-Path $FoundUe "Engine\Build\BatchFiles\RunUAT.bat"
$BuildBat = Join-Path $FoundUe "Engine\Build\BatchFiles\Build.bat"

# runs a tool (or, -DryRun, shows it); the exit code is returned
function Invoke-Tool([string]$Exe, [string[]]$ToolArgs) {
    if ($DryRun) {
        Write-Host "    would run: $Exe $($ToolArgs -join ' ')" -ForegroundColor DarkGray
        return 0
    }
    # its output to the console, not into this function's return value (PowerShell returns every
    # uncaptured line), so the caller gets the exit code alone
    & $Exe @ToolArgs | Out-Host
    return $LASTEXITCODE
}

# -----------------------------------------------------------------------------
# 2. Check the C++ toolchain and disk before an hour of work fails on it
# -----------------------------------------------------------------------------
if ($OnWindows) {
    $Missing = @()
    $VsWhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    $VsPath = if (Test-Path $VsWhere) { & $VsWhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath }
    if (-not $VsPath) { $Missing += "the MSVC C++ compiler: workload 'Game development with C++' (or 'Desktop development with C++')" }
    # the Windows SDK the engine asks for (Engine\Config\Windows\Windows_SDK.json)
    $SdkJson = Join-Path $FoundUe "Engine\Config\Windows\Windows_SDK.json"
    $SdkMin = [version]"10.0.19041.0"; $SdkMain = "10.0.22621.0"
    if (Test-Path $SdkJson) {
        # the file has // comments: keep only the two lines needed
        $Raw = Get-Content $SdkJson -Raw
        if ($Raw -match '"MinVersion"\s*:\s*"([\d.]+)"') { $SdkMin = [version]$Matches[1] }
        if ($Raw -match '"MainVersion"\s*:\s*"([\d.]+)"') { $SdkMain = $Matches[1] }
    }
    $SdkInclude = "${env:ProgramFiles(x86)}\Windows Kits\10\Include"
    $Sdks = @(if (Test-Path $SdkInclude) {
        Get-ChildItem $SdkInclude -Directory | Where-Object { Test-Path (Join-Path $_.FullName "um\windows.h") } |
            ForEach-Object { try { [version]$_.Name } catch { } } | Where-Object { $_ -ge $SdkMin }
    })
    if ($Sdks.Count -eq 0) { $Missing += "a Windows SDK $SdkMin or newer: individual component 'Windows 11 SDK ($SdkMain)'" }
    # the engine's C# tools build against the .NET Framework SDK (NETFXSDK\4.x)
    $NetFx = "${env:ProgramFiles(x86)}\Windows Kits\NETFXSDK"
    if (-not (Test-Path $NetFx) -or -not (Get-ChildItem $NetFx -Directory -Filter "4.*" -ErrorAction SilentlyContinue)) {
        $Missing += "the .NET Framework SDK: workload '.NET desktop development', component '.NET Framework 4.8 SDK'"
    }
    if ($Missing.Count -gt 0) {
        Write-Host ""
        Write-Host "[!] Missing for building C++:" -ForegroundColor Red
        $Missing | ForEach-Object { Write-Host "    - $_" -ForegroundColor Red }
        Write-Host "    Visual Studio Installer -> Visual Studio 2022 -> Modify, add them, then run this again." -ForegroundColor Yellow
        if (-not $DryRun) { exit 1 }
    }
    else {
        Write-Host "[+] C++: $VsPath, Windows SDK $(($Sdks | Sort-Object -Descending)[0])" -ForegroundColor Green
    }
    # cooking writes the derived data cache, the cooked content and the build: tens of GB
    $Free = (Get-PSDrive ($ProjectDir.Substring(0, 1))).Free / 1GB
    if ($Package -and $Free -lt 30) {
        Write-Host "[!] Only $([int]$Free) GB free on $($ProjectDir.Substring(0, 2)): a first cook needs about 30 GB." -ForegroundColor Yellow
    }
}

# -----------------------------------------------------------------------------
# 3. Clean if requested
# -----------------------------------------------------------------------------
if ($Clean) {
    Write-Host "[*] Cleaning Binaries and Intermediate folders..." -ForegroundColor Yellow
    if (-not $DryRun) {
        Remove-Item -Recurse -Force (Join-Path $ProjectDir "Binaries\Win64") -ErrorAction SilentlyContinue
        Remove-Item -Recurse -Force (Join-Path $ProjectDir "Intermediate\Build\Win64") -ErrorAction SilentlyContinue
    }
}

# -----------------------------------------------------------------------------
# 4. Build C++ Modules (UnrealBuildTool): the editor cooks; packaging builds the game itself
# -----------------------------------------------------------------------------
Write-Host ""
Write-Host "[*] Compiling DesktopForcesEditor (Win64 Development)..." -ForegroundColor Cyan
$Code = Invoke-Tool $BuildBat @("DesktopForcesEditor", "Win64", "Development", "-Project=$Uproject", "-WaitMutex", "-NoUBA")
if ($Code -ne 0) {
    Write-Host "[!] Editor build failed with exit code $Code" -ForegroundColor Red
    exit $Code
}

if (-not $Package) {
    Write-Host "[*] Compiling DesktopForces Game ($Config)..." -ForegroundColor Cyan
    $Code = Invoke-Tool $BuildBat @("DesktopForces", "Win64", $Config, "-Project=$Uproject", "-WaitMutex", "-NoUBA")
    if ($Code -ne 0) {
        Write-Host "[!] Game build failed with exit code $Code" -ForegroundColor Red
        exit $Code
    }
    exit 0
}

# -----------------------------------------------------------------------------
# 5. Cook & Package (BuildCookRun)
# -----------------------------------------------------------------------------
# the game lands right in it: UAT adds no Windows\ folder to an archive path that already names the platform
$OutputDir = Join-Path $ProjectDir "Build\Windows"
if ($Public) { $OutputDir = "$OutputDir-public" }
Write-Host ""
Write-Host "[*] Cooking and Packaging game for Windows ($Config)..." -ForegroundColor Cyan
Write-Host "    Output directory: $OutputDir" -ForegroundColor Cyan

# content packs -> their own paks (bGenerateChunks): label each pack root (cheap once they exist)
$EditorCmd = Join-Path $FoundUe "Engine\Binaries\Win64\UnrealEditor-Cmd.exe"
$LabelLog = Join-Path $ProjectDir "Saved\Logs\PackLabels.log"
$Code = Invoke-Tool $EditorCmd @($Uproject, "-run=DFImport", "-steps=packs", "-unattended", "-nopause", "-nosplash", "-abslog=$LabelLog")
if ($Code -ne 0) {
    Write-Host "[!] Labelling the content packs failed with exit code $Code" -ForegroundColor Red
    exit $Code
}
# only one of two same-named labels counts: the other's folder would fall into the core pak
if (-not $DryRun -and (Select-String -Path $LabelLog -Pattern "duplicate PrimaryAssetID" -Quiet)) {
    Write-Host "[!] Two pack labels share a name (Saved\Logs\PackLabels.log)" -ForegroundColor Red
    exit 1
}

# every built map: the game's own and, unless -Public, the content plugins'
$MapDirs = @(
    @{ Dir = "Content/Maps"; Root = "/Game/Maps" },
    @{ Dir = "Content/MF/Maps"; Root = "/Game/MF/Maps" },
    @{ Dir = "Plugins/MobileForcesClassic/Content/Maps"; Root = "/MobileForcesClassic/Maps" },
    @{ Dir = "Plugins/MobileForcesUnverified/Content/Maps"; Root = "/MobileForcesUnverified/Maps" }
)
$Maps = @()
foreach ($m in $MapDirs) {
    if ($Public -and $m.Root -like "/MobileForces*") { continue }
    $Full = Join-Path $ProjectDir $m.Dir
    if (Test-Path $Full) {
        Get-ChildItem $Full -Filter *.umap | Sort-Object Name | ForEach-Object { $Maps += "$($m.Root)/$($_.BaseName)" }
    }
}
Write-Host "    Maps: $($Maps.Count)" -ForegroundColor Cyan

$CommonArgs = @("BuildCookRun", "-project=$Uproject", "-noP4", "-utf8output", "-unattended", "-platform=Win64",
    "-clientconfig=$Config", "-pak", "-iostore", "-compressed")
if ($Config -eq "Shipping") { $CommonArgs += "-nodebuginfo" }
$UatArgs = $CommonArgs + @("-build", "-cook", "-stage", "-prereqs")
# (a checkout without the converted maps - CI - still packages: the menu and what the always-cook
# folders hold)
if ($Maps.Count -gt 0) { $UatArgs += "-map=$($Maps -join '+')" }
if (-not $Downloads) { $UatArgs += @("-archive", "-archivedirectory=$OutputDir") }

# the content build id (UDFDownloads): only in a -Downloads build, only while it's made - the paks
# on the server must come from the same cook as the game asking for them
$BuildIdFile = Join-Path $ProjectDir "Content\Downloads\ContentBuildId.txt"
if (-not $DryRun) {
    New-Item -ItemType Directory -Force (Split-Path $BuildIdFile) | Out-Null # staged as UFS (DefaultGame.ini)
    Remove-Item $BuildIdFile -ErrorAction SilentlyContinue
}
$BuildId = $null
if ($Downloads) {
    $Commit = (& git -C $ProjectDir rev-parse --short HEAD 2>$null)
    if (-not $Commit) { $Commit = "local" }
    $BuildId = (Get-Date).ToUniversalTime().ToString("yyyyMMdd-HHmmss") + "-$Commit"
    if (-not $DryRun) { Set-Content $BuildIdFile $BuildId -NoNewline }
    Write-Host "    Content build: $BuildId" -ForegroundColor Cyan
}

# -Public: the content plugins aren't listed in the project (on when present): list them as off
# for this build only, back the way it was whatever happens
$Backup = "$Uproject.bak"
if ($Public -and -not $DryRun) {
    Copy-Item $Uproject $Backup -Force
    $Json = Get-Content $Uproject -Raw | ConvertFrom-Json
    $Json.Plugins = @($Json.Plugins | Where-Object { $_.Name -notin @("MobileForcesClassic", "MobileForcesUnverified") }) +
        @([pscustomobject]@{ Name = "MobileForcesClassic"; Enabled = $false }, [pscustomobject]@{ Name = "MobileForcesUnverified"; Enabled = $false })
    $Json | ConvertTo-Json -Depth 10 | Set-Content $Uproject -Encoding UTF8
}
try {
    $Code = Invoke-Tool $RunUAT $UatArgs
    if ($Code -eq 0 -and $Downloads) {
        # the packs other than core out of the staged build (Tools/downloads.py), then archive the rest
        $Split = "$OutputDir-downloads"
        $Python = Get-Command python -ErrorAction SilentlyContinue
        if (-not $Python) { $Python = Get-Command python3 -ErrorAction SilentlyContinue }
        $PyArgs = @()
        if (-not $Python) { $Python = Get-Command py -ErrorAction SilentlyContinue; $PyArgs = @("-3") }
        if (-not $Python) { throw "-Downloads needs Python 3.11+" }
        $Code = Invoke-Tool $Python.Source ($PyArgs + @((Join-Path $PSScriptRoot "downloads.py"),
            (Join-Path $ProjectDir "Saved\StagedBuilds\Windows\DesktopForces\Content\Paks"),
            (Join-Path $ProjectDir "Saved\Cooked\Windows\DesktopForces\Metadata\ChunkManifest"),
            $Split, $BuildId, "Windows"))
        if ($Code -eq 0) {
            $Code = Invoke-Tool $RunUAT ($CommonArgs + @("-skipcook", "-skipstage", "-skippak", "-archive", "-archivedirectory=$OutputDir"))
        }
    }
}
finally {
    if ($Public -and (Test-Path $Backup)) { Move-Item $Backup $Uproject -Force }
    if (-not $DryRun) { Remove-Item $BuildIdFile -ErrorAction SilentlyContinue }
}

if ($Code -ne 0) {
    Write-Host "[!] Packaging failed with exit code $Code" -ForegroundColor Red
    exit $Code
}

# one file for the team: tar (built into Windows 10+) writes zips without Compress-Archive's 2 GB
# limit per file
$ZipFile = $null
if ($Zip) {
    $ZipFile = Join-Path (Split-Path $OutputDir) ("DesktopForces-Win64-$Config$(if ($Public) { '-public' })-" + (Get-Date -Format "yyyyMMdd") + ".zip")
    Write-Host "[*] Zipping to $ZipFile ..." -ForegroundColor Cyan
    $Code = Invoke-Tool "tar.exe" @("-a", "-c", "-f", $ZipFile, "-C", (Split-Path $OutputDir), (Split-Path $OutputDir -Leaf))
    if ($Code -ne 0) {
        Write-Host "[!] Zipping failed with exit code $Code" -ForegroundColor Red
        exit $Code
    }
}

Write-Host ""
Write-Host "==========================================================" -ForegroundColor Green
Write-Host " Build & Packaging $(if ($DryRun) { 'dry run done' } else { 'Successful!' })" -ForegroundColor Green
Write-Host " Output: $OutputDir\DesktopForces.exe" -ForegroundColor Green
if ($ZipFile) { Write-Host " Zip: $ZipFile" -ForegroundColor Green }
if ($Downloads) {
    Write-Host " Upload the contents of $OutputDir-downloads\Downloads to the download server" -ForegroundColor Green
    Write-Host " $OutputDir-downloads\Sideload: the classic pack, for your own machines only" -ForegroundColor Yellow
}
Write-Host "==========================================================" -ForegroundColor Green
