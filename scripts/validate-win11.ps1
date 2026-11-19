# Non-disruptive Windows 11 acceptance on a physical machine; never changes WLAN state.
param([Parameter(Mandatory)][string]$Zip, [Parameter(Mandatory)][string]$ReleaseFolder,
      [string]$OutputDirectory = (Join-Path $env:TEMP 'CampusLink validation'),
      [switch]$RequireDellAx201, [switch]$TryVulkan)
$ErrorActionPreference = 'Stop'
[void][IO.Directory]::CreateDirectory($OutputDirectory)
$temp = Join-Path ([IO.Path]::GetTempPath()) ('CampusLink validation ' + [guid]::NewGuid())
$results = [ordered]@{ windows = 'NOT_RUN'; integrity = 'NOT_RUN'; sdl = 'NOT_RUN'; imgui = 'NOT_RUN';
    inventory = 'NOT_RUN'; simulation_profile = 'NOT_RUN'; dell_ax201 = 'NOT_TESTED_ON_HARDWARE';
    vulkan = 'NOT_TESTED_ON_HARDWARE'; wlan_field = 'NOT_TESTED_ON_HARDWARE' }
function Invoke-LocalTest([string]$Executable, [string]$Argument, [string]$Key) {
    $params = [Diagnostics.ProcessStartInfo]::new()
    $params.FileName = $Executable
    $params.Arguments = $Argument
    $params.WorkingDirectory = [IO.Path]::GetDirectoryName($Executable)
    $params.UseShellExecute = $false
    $params.RedirectStandardOutput = $true
    $params.RedirectStandardError = $true
    $process = [Diagnostics.Process]::Start($params)
    try {
        $out = $process.StandardOutput.ReadToEndAsync(); $err = $process.StandardError.ReadToEndAsync()
        if (-not $process.WaitForExit(30000)) { $process.Kill(); $process.WaitForExit(); throw "$Argument timed out" }
        $out.GetAwaiter().GetResult(), $err.GetAwaiter().GetResult() |
            Set-Content -LiteralPath (Join-Path $OutputDirectory ($Key + '.log')) -Encoding UTF8
        if ($process.ExitCode -ne 0) { throw "$Argument returned $($process.ExitCode)" }
        $results[$Key] = 'PASS'
    } catch { $results[$Key] = 'FAIL'; throw }
    finally { $process.Dispose() }
}
try {
    $os = Get-CimInstance Win32_OperatingSystem
    if ($env:PROCESSOR_ARCHITECTURE -ne 'AMD64' -or $os.ProductType -ne 1 -or [int]$os.BuildNumber -lt 22000) {
        $results.windows = 'FAIL'; throw 'Run this on Windows 11 x64 client, not a Windows Server runner.'
    }
    $results.windows = 'PASS'
    $machine = Get-CimInstance Win32_ComputerSystem
    $adapter = @(Get-CimInstance Win32_PnPSignedDriver -Filter "DeviceClass='NET'" |
        Where-Object { $_.DeviceName -match '(?i)Intel.*\bAX201\b' })
    $isDell = $machine.Manufacturer -match 'Dell' -and $machine.Model -match '\bPrecision\s+7750\b'
    $isPhysicalDell = $isDell -and $adapter.Count -gt 0
    if ($isPhysicalDell) { $results.dell_ax201 = 'PASS (model and signed driver inventory only)' }
    elseif ($RequireDellAx201) { $results.dell_ax201 = 'FAIL'; throw 'Precision 7750 and signed AX201 driver were not both observed' }
    [pscustomobject]@{ Build = $os.BuildNumber; Model = $machine.Model; Vendor = $machine.Manufacturer;
        AX201 = @($adapter | Select-Object -Property @('DeviceName','DriverVersion','DriverProviderName','DriverDate')) } |
        ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $OutputDirectory 'machine.json') -Encoding UTF8
    try {
        & (Join-Path $PSScriptRoot 'verify-release.ps1') -Archive $Zip -ReleaseFolder $ReleaseFolder |
            Set-Content -LiteralPath (Join-Path $OutputDirectory 'integrity.log') -Encoding UTF8
        Add-Type -AssemblyName System.IO.Compression.FileSystem
        [IO.Compression.ZipFile]::ExtractToDirectory((Get-Item -LiteralPath $Zip).FullName, $temp)
        $results.integrity = 'PASS'
    } catch { $results.integrity = 'FAIL'; throw }
    $exe = Join-Path $temp 'CampusLink.exe'
    foreach ($test in @(@('--self-test','sdl'), @('--ui-smoke','imgui'), @('--inventory-smoke','inventory'), @('--profile','simulation_profile'))) {
        Invoke-LocalTest $exe $test[0] $test[1]
    }
    if ($TryVulkan) { Invoke-LocalTest $exe '--graphics-smoke' 'vulkan' }
} finally {
    $results | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $OutputDirectory 'results.json') -Encoding UTF8
    if (Test-Path -LiteralPath $temp) { Remove-Item -LiteralPath $temp -Recurse -Force }
    Write-Host "Evidence: $OutputDirectory"
}
