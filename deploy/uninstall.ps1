<#
.SYNOPSIS
    Removes the ScanToAlibre add-on from Alibre Design.

.DESCRIPTION
    Deletes the registry value under HKLM\SOFTWARE\Alibre Design Add-Ons and
    the folder %ProgramData%\Alibre AddOns\ScanToAlibre\. Needs administrator
    rights; re-launches itself elevated if necessary. Close Alibre Design first.
#>

$ErrorActionPreference = 'Stop'

$AddOnName = 'ScanToAlibre'
$AddOnGuid = '{3B8754FC-F18D-4AE9-887D-E48DD9EE5DCB}'   # must match ScanToAlibre.adc
$RegKey    = 'SOFTWARE\Alibre Design Add-Ons'

$principal = New-Object Security.Principal.WindowsPrincipal([Security.Principal.WindowsIdentity]::GetCurrent())
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    Write-Host 'Administrator rights are required - relaunching elevated...'
    $argList = @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', "`"$PSCommandPath`"")
    Start-Process -FilePath 'powershell.exe' -ArgumentList $argList -Verb RunAs -Wait
    exit
}

try {
    if (Get-Process -Name 'Alibre Design' -ErrorAction SilentlyContinue) {
        throw 'Alibre Design is running. Save your work, close Alibre Design, then run this again.'
    }

    $key = [Microsoft.Win32.Registry]::LocalMachine.OpenSubKey($RegKey, $true)
    if ($key) {
        try {
            if ($null -ne $key.GetValue($AddOnGuid)) {
                $key.DeleteValue($AddOnGuid)
                Write-Host "Removed registry value $AddOnGuid"
            }
        }
        finally {
            $key.Close()
        }
    }

    $targetDir = Join-Path $env:ProgramData "Alibre AddOns\$AddOnName"
    if (Test-Path $targetDir) {
        Remove-Item -Path $targetDir -Recurse -Force
        Write-Host "Deleted $targetDir"
    }

    Write-Host 'ScanToAlibre add-on uninstalled.' -ForegroundColor Green
}
catch {
    Write-Host "Uninstall failed: $($_.Exception.Message)" -ForegroundColor Red
}

if ($Host.Name -eq 'ConsoleHost') {
    Write-Host ''
    Read-Host 'Press Enter to close'
}
