# =============================================================================
# build_windows.ps1 - DesktopForces Windows Builder & Packager
#
# Usage:
#   powershell -ExecutionPolicy Bypass -File Tools\build_windows.ps1
#   powershell -ExecutionPolicy Bypass -File Tools\build_windows.ps1 -Config Development
#   powershell -ExecutionPolicy Bypass -File Tools\build_windows.ps1 -Package:$false   (compile only)
#   powershell -ExecutionPolicy Bypass -File Tools\build_windows.ps1 -Clean
#   powershell -ExecutionPolicy Bypass -File Tools\build_windows.ps1 -Public   (no Classic / Unverified content)
#   powershell -ExecutionPolicy Bypass -File Tools\build_windows.ps1 -Zip      (also one .zip to hand around)
#   pwsh Tools/build_windows.ps1 -DryRun   (print what would run; works on Linux too)
#
# Same build as Tools/package.sh on Linux: every map passed by name (the cooker doesn't take maps
# from the always-cook folders), iostore + compressed paks.
#
# The engine is the project's EngineAssociation (DesktopForces.uproject, "5.8"): -UePath or
# $env:UE_ROOT if given, else where the Epic Games Launcher registered it (registry,
# LauncherInstalled.dat), else <drive>:\Program Files\Epic Games\UE_<version> on any drive.
# =============================================================================

param (
    [ValidateSet("Development", "Shipping")]
    [string]$Config = "Shipping",
    [switch]$Package = $true,                 # Full Cook & Package (default true; -Package:$false compiles only)
    [switch]$Clean = $false,                  # Clean Intermediate & Binaries before build
    [switch]$Public = $false,                 # Leave out the Classic and Unverified content plugins
    [switch]$DryRun = $false,                 # Print the commands instead of running them
    [switch]$Zip = $false,                    # Pack the finished build into one .zip next to it
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
$SearchPaths = @($UePath, $env:UNREAL_ENGINE_PATH)
if ($OnWindows) {
    # Epic Games Launcher installs
    $Key = "HKLM:\SOFTWARE\EpicGames\Unreal Engine\$Assoc"
    if (Test-Path $Key) { $SearchPaths += (Get-ItemProperty $Key).InstalledDirectory }
    $Dat = Join-Path $env:ProgramData "Epic\UnrealEngineLauncher\LauncherInstalled.dat"
    if (Test-Path $Dat) {
        $SearchPaths += (Get-Content $Dat -Raw | ConvertFrom-Json).InstallationList |
            Where-Object { $_.AppName -eq "UE_$Assoc" } | ForEach-Object { $_.InstallLocation }
    }
    # source builds registered with UnrealVersionSelector
    $Builds = "HKCU:\SOFTWARE\Epic Games\Unreal Engine\Builds"
    if (Test-Path $Builds) {
        $SearchPaths += (Get-ItemProperty $Builds).PSObject.Properties | Where-Object { $_.Name -notlike "PS*" } | ForEach-Object { $_.Value }
    }
    # the usual places on every drive
    foreach ($Drive in (Get-PSDrive -PSProvider FileSystem | ForEach-Object { $_.Root })) {
        $SearchPaths += (Join-Path $Drive "Program Files\Epic Games\UE_$Assoc"), (Join-Path $Drive "Epic Games\UE_$Assoc")
    }
}

$FoundUe = $null
foreach ($path in $SearchPaths) {
    if ($path -and (Test-Path (Join-Path $path "Engine\Build\BatchFiles\RunUAT.bat"))) {
        $FoundUe = (Resolve-Path $path).Path
        break
    }
}

if (-not $FoundUe -and $DryRun) {
    $FoundUe = "<UE_$Assoc>"
}
elseif (-not $FoundUe) {
    Write-Host ""
    Write-Host "[!] Unreal Engine $Assoc was not found (looked at: $(($SearchPaths | Where-Object { $_ }) -join '; '))." -ForegroundColor Red
    Write-Host ""
    Write-Host "HOW TO GET A MINIMAL ~12 GB UNREAL ENGINE INSTALLATION:" -ForegroundColor Yellow
    Write-Host "  1. Open Epic Games Launcher on Windows" -ForegroundColor Yellow
    Write-Host "  2. Go to Unreal Engine -> Library -> Install UE $Assoc" -ForegroundColor Yellow
    Write-Host "  3. Click the dropdown arrow next to 'Install' -> Options" -ForegroundColor Yellow
    Write-Host "  4. UNCHECK the following to save ~50 GB:" -ForegroundColor Yellow
    Write-Host "     [ ] Editor symbols for debugging   (Saves ~35 GB!)" -ForegroundColor Yellow
    Write-Host "     [ ] Starter Content               (Saves ~3 GB)" -ForegroundColor Yellow
    Write-Host "     [ ] Templates and Feature Packs   (Saves ~3 GB)" -ForegroundColor Yellow
    Write-Host "     [ ] Engine Source                 (Saves ~3 GB)" -ForegroundColor Yellow
    Write-Host "     [ ] Android / Linux Platforms     (Saves ~5 GB)" -ForegroundColor Yellow
    Write-Host "  Total install size will drop from ~70 GB to only ~12-14 GB!" -ForegroundColor Green
    Write-Host "  (or from Linux, onto the Windows drive: python3 Tools/download_ue_windows.py)" -ForegroundColor Green
    Write-Host ""
    Write-Host "Or specify your existing path: -UePath 'X:\Path\To\UE_$Assoc'" -ForegroundColor Cyan
    exit 1
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
$OutputDir = Join-Path $ProjectDir "Build\Windows"
if ($Public) { $OutputDir = "$OutputDir-public" }
Write-Host ""
Write-Host "[*] Cooking and Packaging game for Windows ($Config)..." -ForegroundColor Cyan
Write-Host "    Output directory: $OutputDir" -ForegroundColor Cyan

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

$UatArgs = @("BuildCookRun", "-project=$Uproject", "-noP4", "-utf8output", "-unattended", "-platform=Win64",
    "-clientconfig=$Config", "-build", "-cook", "-stage", "-pak", "-iostore",
    "-compressed", "-prereqs", "-archive", "-archivedirectory=$OutputDir")
# (a checkout without the converted maps - CI - still packages: the menu and what the always-cook
# folders hold)
if ($Maps.Count -gt 0) { $UatArgs += "-map=$($Maps -join '+')" }
if ($Config -eq "Shipping") { $UatArgs += "-nodebuginfo" }

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
}
finally {
    if ($Public -and (Test-Path $Backup)) { Move-Item $Backup $Uproject -Force }
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
    $Code = Invoke-Tool "tar.exe" @("-a", "-c", "-f", $ZipFile, "-C", $OutputDir, "Windows")
    if ($Code -ne 0) {
        Write-Host "[!] Zipping failed with exit code $Code" -ForegroundColor Red
        exit $Code
    }
}

Write-Host ""
Write-Host "==========================================================" -ForegroundColor Green
Write-Host " Build & Packaging $(if ($DryRun) { 'dry run done' } else { 'Successful!' })" -ForegroundColor Green
Write-Host " Output: $OutputDir\Windows (DesktopForces.exe)" -ForegroundColor Green
if ($ZipFile) { Write-Host " Zip: $ZipFile" -ForegroundColor Green }
Write-Host "==========================================================" -ForegroundColor Green
