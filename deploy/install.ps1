<#
.SYNOPSIS
    Installs the ScanToAlibre add-on into Alibre Design.

.DESCRIPTION
    1. Copies ScanToAlibre.dll, ScanToAlibre.adc, ScanToAlibre.ico and the button images (ShowScan.ico and the others) from the build output, and the help page from help\, to
       %ProgramData%\Alibre AddOns\ScanToAlibre\
    2. Registers the add-on under HKLM\SOFTWARE\Alibre Design Add-Ons
       (value name = add-on GUID, data = install folder).

    Needs administrator rights; re-launches itself elevated if necessary.
    Close Alibre Design first (the DLL is locked while Alibre runs), and
    restart it afterwards - Alibre only reads add-ons at startup.

.PARAMETER Configuration
    Build configuration to install: Release (default) or Debug.
#>
param(
    [ValidateSet('Release', 'Debug')]
    [string]$Configuration = 'Release'
)

$ErrorActionPreference = 'Stop'

$AddOnName = 'ScanToAlibre'
$AddOnGuid = '{3B8754FC-F18D-4AE9-887D-E48DD9EE5DCB}'   # must match ScanToAlibre.adc
$RegKey    = 'SOFTWARE\Alibre Design Add-Ons'

# --- Elevation ---------------------------------------------------------------
$principal = New-Object Security.Principal.WindowsPrincipal([Security.Principal.WindowsIdentity]::GetCurrent())
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    Write-Host 'Administrator rights are required - relaunching elevated...'
    $argList = @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', "`"$PSCommandPath`"", '-Configuration', $Configuration)
    Start-Process -FilePath 'powershell.exe' -ArgumentList $argList -Verb RunAs -Wait
    exit
}

try {
    # --- Locate build output --------------------------------------------------
    $projectDir = Split-Path -Parent $PSScriptRoot
    $buildDir   = Join-Path $projectDir "bin\x64\$Configuration"
    $files      = @("$AddOnName.dll", "$AddOnName.adc", "$AddOnName.ico", "ShowScan.ico",
                    "Load.ico", "Refine.ico", "Orient.ico", "Section.ico", "SectionSeries.ico", "Surface.ico", "Silhouette.ico", "Compare.ico", "Help.ico")

    foreach ($f in $files) {
        if (-not (Test-Path (Join-Path $buildDir $f))) {
            throw "Missing '$f' in '$buildDir'. Build the $Configuration|x64 configuration first."
        }
    }

    # --- Alibre must not be running ------------------------------------------
    if (Get-Process -Name 'Alibre Design' -ErrorAction SilentlyContinue) {
        throw 'Alibre Design is running. Save your work, close Alibre Design, then run this again.'
    }

    # --- Copy files ------------------------------------------------------------
    $targetDir = Join-Path $env:ProgramData "Alibre AddOns\$AddOnName"
    New-Item -ItemType Directory -Path $targetDir -Force | Out-Null   # creates 'Alibre AddOns' too
    foreach ($f in $files) {
        Copy-Item -Path (Join-Path $buildDir $f) -Destination $targetDir -Force
    }
    $pdb = Join-Path $buildDir "$AddOnName.pdb"
    if (Test-Path $pdb) { Copy-Item -Path $pdb -Destination $targetDir -Force }
    Write-Host "Copied add-on files to $targetDir"

    # --- Help ------------------------------------------------------------------
    # The help page, opened from the ? buttons and the ribbon's Help, goes in a help folder beside the add-on.
    $helpDir = Join-Path $targetDir 'help'
    New-Item -ItemType Directory -Path $helpDir -Force | Out-Null
    Copy-Item -Path (Join-Path $projectDir 'help\*') -Destination $helpDir -Recurse -Force
    Write-Host "Copied help to $helpDir"

    # --- Register --------------------------------------------------------------
    # Use the .NET API: New-Item -Force on HKLM:\SOFTWARE fails even when elevated.
    $key = [Microsoft.Win32.Registry]::LocalMachine.CreateSubKey($RegKey)
    try {
        $key.SetValue($AddOnGuid, $targetDir, [Microsoft.Win32.RegistryValueKind]::String)
    }
    finally {
        $key.Close()
    }
    Write-Host "Registered $AddOnGuid -> $targetDir under HKLM\$RegKey"

    Write-Host ''
    Write-Host 'Done. Start Alibre Design and look for the "ScanToAlibre" ribbon tab.' -ForegroundColor Green
}
catch {
    Write-Host "Install failed: $($_.Exception.Message)" -ForegroundColor Red
}

if ($Host.Name -eq 'ConsoleHost') {
    Write-Host ''
    Read-Host 'Press Enter to close'
}
