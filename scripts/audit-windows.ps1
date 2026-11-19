# Run locally as a standard user. No network scan, profile export or state changes.
$ErrorActionPreference = 'Stop'
$failed = $false

function Show-Section([string]$Name, [scriptblock]$Inspect) {
    Write-Host "`n=== $Name ==="
    try {
        & $Inspect
    } catch {
        $script:failed = $true
        Write-Output "[FAIL] $($_.Exception.Message)"
    }
}

Write-Output "CAMPUSLINK local audit | UTC $([datetime]::UtcNow.ToString('o'))"
Show-Section 'Windows' {
    Get-CimInstance Win32_OperatingSystem |
        Select-Object -Property @('Caption', 'Version', 'BuildNumber', 'OSArchitecture', 'ProductType') | Format-List
}
Show-Section 'System, firmware and memory' {
    Get-CimInstance Win32_ComputerSystem |
        Select-Object -Property @('Manufacturer', 'Model', 'TotalPhysicalMemory') | Format-List
    Get-CimInstance Win32_BIOS | Select-Object SMBIOSBIOSVersion | Format-List
    Get-CimInstance Win32_Processor |
        Select-Object -Property @('Name', 'NumberOfCores', 'NumberOfLogicalProcessors') | Format-List
}
Show-Section 'Local volumes (bytes; no labels or serials)' {
    Get-CimInstance Win32_LogicalDisk -Filter 'DriveType = 3' |
        Select-Object -Property @('DeviceID', 'FileSystem', 'Size', 'FreeSpace') | Format-List
}
Show-Section 'Graphics and Vulkan loader files (not a device test)' {
    Get-CimInstance Win32_VideoController |
        Select-Object -Property @('Name', 'DriverVersion', 'DriverDate', 'Status') | Format-List
    $loader = Join-Path $env:SystemRoot 'System32\vulkan-1.dll'
    if (Test-Path -LiteralPath $loader -PathType Leaf) {
        (Get-Item -LiteralPath $loader).VersionInfo | Select-Object FileVersion | Format-List
    } else { Write-Output '[WARN] System32 Vulkan loader file not found.' }
    Write-Output 'Vulkan physical-device and swapchain tests: NOT_RUN.'
}
Show-Section 'Network devices and signed drivers (no MAC, profile or IP export)' {
    $devices = @(Get-CimInstance Win32_PnPEntity -Filter "PNPClass='Net'")
    $drivers = @(Get-CimInstance Win32_PnPSignedDriver -Filter "DeviceClass='NET'")
    if (-not $devices.Count) { Write-Output '[WARN] No Net-class PnP entries returned.' }
    foreach ($device in $devices) {
        $driver = $drivers | Where-Object { $_.DeviceID -eq $device.DeviceID } | Select-Object -First 1
        [pscustomobject]@{
            Device = $device.Name
            Manufacturer = $device.Manufacturer
            HardwareId = $device.HardwareID -join '; '
            PnpStatus = $device.Status
            PnpError = $device.ConfigManagerErrorCode
            DriverProvider = $driver.DriverProviderName
            DriverVersion = $driver.DriverVersion
            DriverDate = $driver.DriverDate
            Confidence = 'DRIVER_REPORTED (CIM/PnP)'
        } | Format-List
    }
}
Show-Section 'WLAN AutoConfig' {
    Get-Service -Name WlanSvc | Select-Object -Property @('Name', 'Status', 'StartType') | Format-List
}
Write-Output 'STA concurrency: UNKNOWN (no capability operation attempted).'
Write-Output 'Radio, permissions, Vulkan rendering, performance and handoff: NOT_RUN.'
if ($failed) { exit 1 }
exit 0
