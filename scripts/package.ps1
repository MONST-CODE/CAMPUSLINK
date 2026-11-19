# Called from scripts/build.ps1 after mandatory builds and tests.
param([Parameter(Mandatory)][string]$Root, [string]$Version, [string]$BinaryDirectory,
      [string]$Compiler, [string]$BuildLog, [switch]$ReleaseAll)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.IO.Compression.FileSystem
$dist = Join-Path $Root 'dist'
$packages = Join-Path $dist 'packages'
$releases = Join-Path $dist 'releases'
$key = 'CAMPUSLINK-' + $Version + '-windows-x64-portable'
$temp = Join-Path ([IO.Path]::GetTempPath()) ('CampusLink package test ' + [guid]::NewGuid())
$stage = Join-Path $temp 'payload'
$extract = Join-Path $temp 'extracted with spaces'
$zip = Join-Path $temp ($key + '.zip')
$encoding = [Text.UTF8Encoding]::new($false)
function Write-Utf8([string]$Path, [string]$Text) { [IO.File]::WriteAllText($Path, $Text, $encoding) }
function Hash([string]$File) { (Get-FileHash -LiteralPath $File -Algorithm SHA256).Hash.ToLowerInvariant() }
function Run-Extracted([string]$Exe, [string]$TestName) {
    $start = [Diagnostics.ProcessStartInfo]::new()
    $start.FileName = $Exe
    $start.Arguments = $TestName
    $start.WorkingDirectory = $extract
    $start.UseShellExecute = $false
    $start.RedirectStandardOutput = $true
    $start.RedirectStandardError = $true
    $p = [Diagnostics.Process]::Start($start)
    if (-not $p) { throw "Could not start extracted executable: $TestName" }
    try {
        $stdout = $p.StandardOutput.ReadToEndAsync()
        $stderr = $p.StandardError.ReadToEndAsync()
        if (-not $p.WaitForExit(20000)) { $p.Kill(); $p.WaitForExit(); throw "Extracted smoke test $TestName timed out" }
        $code = $p.ExitCode
        $stdout.GetAwaiter().GetResult().Trim()
        $stderr.GetAwaiter().GetResult().Trim()
        if ($code -ne 0) { throw "Extracted smoke test $TestName failed: $code" }
    } finally { $p.Dispose() }
}
[void][IO.Directory]::CreateDirectory($stage)
try {
    $licenses = Join-Path $stage 'LICENSES'
    $config = Join-Path $stage 'config'
    [void][IO.Directory]::CreateDirectory($licenses)
    [void][IO.Directory]::CreateDirectory($config)
    foreach ($file in @('CampusLink.exe','SDL3.dll')) {
        Copy-Item -LiteralPath (Join-Path $BinaryDirectory $file) -Destination (Join-Path $stage $file)
    }
    foreach ($file in @('VERSION','CHANGELOG.md','THIRD_PARTY_NOTICES.md','deps.lock')) {
        Copy-Item -LiteralPath (Join-Path $Root $file) -Destination (Join-Path $stage $file)
    }
    Copy-Item -LiteralPath (Join-Path $Root 'docs\PORTABLE_README.md') -Destination (Join-Path $stage 'README.md')
    $toolsPath = Join-Path $stage 'tools'
    $validationPath = Join-Path $stage 'validation'
    [void][IO.Directory]::CreateDirectory($toolsPath)
    [void][IO.Directory]::CreateDirectory($validationPath)
    Copy-Item -LiteralPath (Join-Path $Root 'tools\clp1_server.py') -Destination $toolsPath
    foreach ($validation in @('audit-windows.ps1', 'validate-win11.ps1', 'verify-release.ps1')) {
        Copy-Item -LiteralPath (Join-Path $Root ('scripts\' + $validation)) -Destination $validationPath
    }
    Copy-Item -LiteralPath (Join-Path $Root 'config\test_scope.example.conf') -Destination $config
    Copy-Item -LiteralPath (Join-Path $Root 'third_party\sdl3\LICENSE.txt') -Destination (Join-Path $licenses 'SDL3.txt')
    Copy-Item -LiteralPath (Join-Path $Root 'third_party\imgui\LICENSE.txt') -Destination (Join-Path $licenses 'Dear-ImGui.txt')
    Copy-Item -LiteralPath (Join-Path $Root 'docs\USER_GUIDE.md') -Destination (Join-Path $stage 'USER_GUIDE.md')
    Copy-Item -LiteralPath (Join-Path $Root 'docs\BUILD_RELEASE.md') -Destination (Join-Path $stage 'BUILD_RELEASE.md')
    foreach ($guide in @('WINDOWS11_VALIDATION.md', 'NETWORK_DIAGNOSTICS.md')) {
        Copy-Item -LiteralPath (Join-Path $Root ('docs\' + $guide)) -Destination $stage
    }
    $commit = & git.exe -C $Root rev-parse HEAD
    if ($LASTEXITCODE -ne 0) { throw 'Cannot resolve Git commit' }
    $changes = @(& git.exe -C $Root status --porcelain)
    if ($LASTEXITCODE -ne 0) { throw 'Cannot inspect Git status' }
    $dumpbin = Join-Path $env:VCToolsInstallDir 'bin\Hostx64\x64\dumpbin.exe'
    $depLines = @(foreach ($file in @('CampusLink.exe','SDL3.dll')) {
        $file
        $dump = @(& $dumpbin /NOLOGO /DEPENDENTS (Join-Path $stage $file))
        if ($LASTEXITCODE -ne 0) { throw "dumpbin /DEPENDENTS failed for $file" }
        $dump | ForEach-Object { $_.Replace($stage + '\', '') }
    })
    Write-Utf8 (Join-Path $stage 'RUNTIME_IMPORTS.txt') (($depLines -join "`n") + "`n")
    $versionLines = @(& $Compiler --version)
    if ($LASTEXITCODE -ne 0) { throw 'Unable to identify the compiler' }
    $created = [datetime]::UtcNow.ToString('o')
    $lock = Get-Content -LiteralPath (Join-Path $Root 'deps.lock') -Raw | ConvertFrom-Json
    $build = [ordered]@{ version = $Version; platform = 'Windows'; architecture = 'x64'; abi = 'MSVC'; configuration = 'Release';
        utc = $created; commit = "$commit"; dirty = ($changes.Count -gt 0); compiler = ($versionLines -join "`n");
        sdk = $env:WindowsSDKVersion.TrimEnd('\'); vcToolset = $env:VCToolsVersion; dependencies = $lock }
    Write-Utf8 (Join-Path $stage 'BUILD_INFO.json') ($build | ConvertTo-Json -Depth 8)
    $notes = "# CAMPUSLINK $Version`n`nWindows x64 candidate package. Dell/AX201: NOT_TESTED_ON_HARDWARE unless separate evidence is supplied.`nSee CHANGELOG.md, USER_GUIDE.md and RUNTIME_IMPORTS.txt.`n"
    Write-Utf8 (Join-Path $stage 'RELEASE_NOTES.md') $notes
    $files = @(Get-ChildItem -LiteralPath $stage -Recurse -File | Sort-Object FullName | ForEach-Object {
        [ordered]@{ path = ($_.FullName.Substring($stage.Length + 1) -replace '\\','/'); bytes = $_.Length; sha256 = (Hash $_.FullName) }
    })
    $build['files'] = $files
    $manifestPath = Join-Path $stage 'manifest.json'
    Write-Utf8 $manifestPath ($build | ConvertTo-Json -Depth 8)
    $manifestHash = Hash $manifestPath
    [IO.Compression.ZipFile]::CreateFromDirectory($stage, $zip, [IO.Compression.CompressionLevel]::Optimal, $false)
    $archive = [IO.Compression.ZipFile]::OpenRead($zip)
    try {
        $expected = @($files | ForEach-Object { $_.path }) + 'manifest.json'
        if ($archive.Entries.Count -ne $expected.Count) { throw 'Unexpected ZIP entry count' }
        $seen = @{}
        foreach ($entry in $archive.Entries) {
            $name = $entry.FullName.Replace('\','/')
            if (-not ($expected -ccontains $name) -or $seen.ContainsKey($name)) { throw "Unexpected/duplicate ZIP entry: $name" }
            $seen[$name] = $true
        }
    } finally { $archive.Dispose() }
    [IO.Compression.ZipFile]::ExtractToDirectory($zip, $extract)
    $extractedManifest = Join-Path $extract 'manifest.json'
    if ((Hash $extractedManifest) -ne $manifestHash) { throw 'Manifest changed during archive creation' }
    $actualManifest = Get-Content -LiteralPath $extractedManifest -Raw -Encoding UTF8 | ConvertFrom-Json
    foreach ($item in $actualManifest.files) {
        $f = Join-Path $extract $item.path
        if ((Get-Item -LiteralPath $f).Length -ne $item.bytes -or (Hash $f) -ne $item.sha256) { throw "File integrity failure: $($item.path)" }
    }
    Run-Extracted (Join-Path $extract 'CampusLink.exe') '--self-test'
    Run-Extracted (Join-Path $extract 'CampusLink.exe') '--ui-smoke'
    $zipHash = Hash $zip
    [void][IO.Directory]::CreateDirectory($packages)
    $packagePath = Join-Path $packages ($key + '.zip')
    Copy-Item -LiteralPath $zip -Destination $packagePath -Force
    if ((Hash $packagePath) -ne $zipHash) { throw 'Packaged ZIP copy differs' }
    Write-Utf8 ($packagePath + '.sha256') "$zipHash  $key.zip`n"
    if ($ReleaseAll) {
        [void][IO.Directory]::CreateDirectory($releases)
        $releasePath = Join-Path $releases ('v' + $Version)
        if (Test-Path -LiteralPath $releasePath) { throw "Local release already exists: $releasePath. Preserve or move it first." }
        $pending = Join-Path $releases ('.pending-' + [guid]::NewGuid())
        [void][IO.Directory]::CreateDirectory($pending)
        try {
            Copy-Item -LiteralPath $zip -Destination (Join-Path $pending ($key + '.zip'))
            if ((Hash (Join-Path $pending ($key + '.zip'))) -ne $zipHash) { throw 'Release ZIP copy differs' }
            Copy-Item -LiteralPath @($manifestPath, (Join-Path $stage 'BUILD_INFO.json'), (Join-Path $stage 'RELEASE_NOTES.md'), (Join-Path $stage 'deps.lock')) -Destination $pending
            if ((Hash (Join-Path $pending 'manifest.json')) -ne $manifestHash) { throw 'Release manifest copy differs' }
            Write-Utf8 (Join-Path $pending 'SHA256SUMS.txt') "$zipHash  $key.zip`n$manifestHash  manifest.json`n"
            Write-Utf8 (Join-Path $pending 'RELEASE_REPORT.md') "# Verification for $Version`n`n- Required build and automated tests: PASS`n- Archive payload path list and SHA-256: PASS`n- Extracted SDL and ImGui smoke from a path with spaces: PASS`n- Dell/AX201: NOT_TESTED_ON_HARDWARE`n- Native visible window / WLAN field tests: NOT_RUN`n- Build log: $([IO.Path]::GetFileName($BuildLog))`n"
            Move-Item -LiteralPath $pending -Destination $releasePath
        } finally { if (Test-Path -LiteralPath $pending) { Remove-Item -LiteralPath $pending -Recurse -Force } }
        "Verified local release: $releasePath"
    }
    "Verified package: $packagePath"
    "SHA256: $zipHash"
} finally {
    Remove-Item -LiteralPath $temp -Recurse -Force
}
