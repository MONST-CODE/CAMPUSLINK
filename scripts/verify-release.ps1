param([Parameter(Mandatory)][string]$Archive, [string]$ReleaseFolder)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.IO.Compression.FileSystem
$archiveFile = Get-Item -LiteralPath $Archive
$checksum = if ($ReleaseFolder) { Join-Path $ReleaseFolder 'SHA256SUMS.txt' } else { $archiveFile.FullName + '.sha256' }
$lines = @(Get-Content -LiteralPath $checksum)
$expected = $lines | Where-Object { $_ -cmatch ('\A[0-9a-f]{64}  ' + [regex]::Escape($archiveFile.Name) + '\z') } | Select-Object -First 1
$hash = (Get-FileHash -LiteralPath $archiveFile.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
if (-not $expected -or $expected.Substring(0,64) -cne $hash) { throw 'Archive SHA-256 does not match its checksum' }
$zip = [IO.Compression.ZipFile]::OpenRead($archiveFile.FullName)
try {
    $items = @($zip.Entries | Where-Object { $_.FullName -ceq 'manifest.json' })
    if ($items.Count -ne 1 -or $items[0].Length -gt 131072) { throw 'Missing, duplicate or oversized manifest' }
    $reader = [IO.StreamReader]::new($items[0].Open())
    try { $manifest = $reader.ReadToEnd() | ConvertFrom-Json } finally { $reader.Dispose() }
    if ($manifest.platform -cne 'Windows' -or $manifest.architecture -cne 'x64' -or $manifest.abi -cne 'MSVC' -or
        $manifest.configuration -cne 'Release' -or $manifest.version -notmatch '^\d+\.\d+\.\d+$') { throw 'Invalid manifest target' }
    if ([version]$manifest.version -ge [version]'0.3.0' -and @($zip.Entries | Where-Object { $_.FullName.Contains('\') }).Count) {
        throw 'ZIP entry uses a Windows path separator'
    }
    if ($zip.Entries.Count -ne $manifest.files.Count + 1) { throw 'ZIP/manifest count mismatch' }
    $sha = [Security.Cryptography.SHA256]::Create()
    $seen = @{}
    try {
        foreach ($file in $manifest.files) {
            $path = [string]$file.path
            if ($path -notmatch '^[a-zA-Z0-9_.-]+(/[a-zA-Z0-9_.-]+)*$' -or $path -match '(^|/)\.{1,2}(/|$)' -or $seen.ContainsKey($path)) { throw 'Unexpected manifest path' }
            $seen[$path] = $true
            $entries = @($zip.Entries | Where-Object { $_.FullName.Replace('\','/') -ceq $path })
            if ($entries.Count -ne 1 -or $entries[0].Length -ne $file.bytes) { throw "Missing/wrong ZIP payload: $path" }
            $stream = $entries[0].Open()
            try { $itemHash = [BitConverter]::ToString($sha.ComputeHash($stream)).Replace('-','').ToLowerInvariant() } finally { $stream.Dispose() }
            if ($itemHash -cne $file.sha256) { throw "SHA-256 mismatch: $path" }
        }
    } finally { $sha.Dispose() }
    $required = @('CampusLink.exe','SDL3.dll','BUILD_INFO.json','RUNTIME_IMPORTS.txt','VERSION','THIRD_PARTY_NOTICES.md',
        'README.md','deps.lock','config/test_scope.example.conf','licenses/SDL3.txt','licenses/Dear-ImGui.txt')
    # Historical archives retain their original layout. Check the complete operator kit for new packages.
    if ([version]$manifest.version -ge [version]'0.2.1') {
        $required += @('USER_GUIDE.md','BUILD_RELEASE.md','WINDOWS11_VALIDATION.md','NETWORK_DIAGNOSTICS.md',
            'tools/clp1_server.py','validation/audit-windows.ps1','validation/validate-win11.ps1','validation/verify-release.ps1')
    }
    if ([version]$manifest.version -ge [version]'0.3.0') {
        $required += @('assets/fonts/Inter.ttf','assets/fonts/JetBrainsMono.ttf','LICENSES/Inter-OFL.txt','LICENSES/JetBrainsMono-OFL.txt')
    }
    foreach ($name in $required) {
        if (-not $seen.ContainsKey($name)) { throw "Required payload absent: $name" }
    }
    if ($ReleaseFolder) {
        $manifestHash = (Get-FileHash -LiteralPath (Join-Path $ReleaseFolder 'manifest.json') -Algorithm SHA256).Hash.ToLowerInvariant()
        if (-not ($lines -ccontains "$manifestHash  manifest.json")) { throw 'External manifest SHA-256 mismatch' }
        $sha = [Security.Cryptography.SHA256]::Create()
        $stream = $items[0].Open()
        try { $zipManifestHash = [BitConverter]::ToString($sha.ComputeHash($stream)).Replace('-','').ToLowerInvariant() }
        finally { $stream.Dispose(); $sha.Dispose() }
        if ($zipManifestHash -cne $manifestHash) { throw 'Manifest differs from archive' }
    }
} finally { $zip.Dispose() }
"Verified SHA-256, manifest and $($manifest.files.Count) payload entries: $hash"
