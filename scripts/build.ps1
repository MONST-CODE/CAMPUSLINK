# Windows PowerShell 5.1. Keep the source ASCII; Unicode marks are built at runtime.
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$build = Join-Path $root 'build'
$command = if ($args.Count -eq 0) { 'release' } else { $args[0].ToLowerInvariant() }
$log = $null
$exitCode = 0
$nativeFailure = 0
$useUnicode = -not $env:CAMPUSLINK_ASCII -and [Console]::OutputEncoding.CodePage -eq 65001
$useColor = -not $env:NO_COLOR -and -not [Console]::IsOutputRedirected
$supportsVT = $useColor -and ($Host.UI.PSObject.Properties['SupportsVirtualTerminal']) -and $Host.UI.SupportsVirtualTerminal
$ansi = @{ Cyan = 96; Blue = 94; Green = 92; Yellow = 93; Red = 91; Gray = 37; DarkGray = 90 }
$stages = @('Environment / toolchain', 'Dependency validation', 'C++20 compilation', 'x64 linking / runtime',
    'Offline smoke tests', 'Portable packaging', 'Release verification')
$results = @('SKIP', 'SKIP', 'SKIP', 'SKIP', 'SKIP', 'SKIP', 'SKIP')

function Write-Line([string]$Text, [ConsoleColor]$Color = 'Gray') {
    if ($supportsVT) {
        $esc = [char]27
        [Console]::WriteLine(('{0}[{1}m{2}{0}[0m' -f $esc, $ansi[$Color.ToString()], $Text))
    } elseif ($useColor) {
        Write-Host $Text -ForegroundColor $Color
    } else { Write-Host $Text }
    if ($log) { Add-Content -LiteralPath $log -Value $Text -Encoding UTF8 }
}

function Invoke-Step([int]$Number, [scriptblock]$Action) {
    $label = '[{0:00}/07] {1}' -f $Number, $stages[$Number - 1]
    Write-Line "  $label | RUNNING" Blue
    $clock = [Diagnostics.Stopwatch]::StartNew()
    $script:nativeFailure = 0
    try {
        & $Action
        $results[$Number - 1] = 'PASS'
        $mark = if ($useUnicode) { [char]0x2713 } else { '+' }
        Write-Line ('    [OK] {0} {1:F2}s | exit 0' -f $mark, $clock.Elapsed.TotalSeconds) Green
    } catch {
        $results[$Number - 1] = 'FAIL'
        $code = if ($nativeFailure) { $nativeFailure } else { 1 }
        Write-Line ('    [FAIL] {0:F2}s | exit {1} | {2}' -f $clock.Elapsed.TotalSeconds, $code, $_.Exception.Message) Red
        throw
    }
}

function Invoke-Native([string]$Exe, [string[]]$Arguments) {
    Write-Line ('    > "{0}" {1}' -f $Exe, (($Arguments | ForEach-Object { '"{0}"' -f $_ }) -join ' '))
    $ErrorActionPreference = 'Continue' # PS 5.1 wraps redirected stderr in ErrorRecord.
    $global:LASTEXITCODE = 1
    $output = @(& $Exe @Arguments 2>&1)
    $code = $LASTEXITCODE
    foreach ($line in $output) { Write-Line ('      ' + $line.ToString()) }
    if ($code -ne 0) {
        $script:nativeFailure = $code
        throw "Native command returned $code."
    }
}

function Find-Tool([string]$Name, [string[]]$Candidates = @()) {
    $tool = Get-Command $Name -CommandType Application -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($tool) { return $tool.Path }
    foreach ($path in $Candidates) {
        if (Test-Path -LiteralPath $path -PathType Leaf) { return $path }
    }
}

function Require-Files([string[]]$Paths) {
    $missing = @($Paths | Where-Object { -not (Test-Path -LiteralPath $_ -PathType Leaf) })
    if ($missing.Count) { throw ('Missing file(s): ' + ($missing -join '; ')) }
}

function Import-VsEnvironment {
    $vswhere = Find-Tool 'vswhere.exe' @("${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe")
    if (-not $vswhere) { throw 'vswhere.exe missing. Install Visual Studio Build Tools with C++ x64 tools and Windows 11 SDK.' }
    $install = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if ($LASTEXITCODE -ne 0) {
        $script:nativeFailure = $LASTEXITCODE
        throw 'vswhere failed.'
    }
    if (-not $install) { throw 'No Visual Studio installation with VC x64 tools was found. Use Visual Studio Installer.' }
    $devcmd = Join-Path ($install | Select-Object -First 1) 'Common7\Tools\VsDevCmd.bat'
    Require-Files @($devcmd)
    $env:CAMPUSLINK_VSDEVCMD = $devcmd
    $helper = Join-Path $PSScriptRoot 'vs-env.bat'
    Write-Line "    Initializing x64 tools with $devcmd"
    $ErrorActionPreference = 'Continue'
    $raw = @(& "$env:SystemRoot\System32\cmd.exe" /d /c call $helper 2>&1)
    $code = $LASTEXITCODE
    $ErrorActionPreference = 'Stop'
    foreach ($line in $raw) {
        if ($line -is [System.Management.Automation.ErrorRecord]) { Write-Line ('      ' + $line.ToString()) }
        elseif ($code -eq 0 -and $line -match '^(PATH|INCLUDE|LIB|LIBPATH|VCToolsInstallDir|VCToolsVersion|WindowsSdkDir|WindowsSDKVersion|VSCMD_ARG_TGT_ARCH)=(.*)$') {
            [Environment]::SetEnvironmentVariable($Matches[1], $Matches[2], 'Process')
        }
    }
    if ($code -ne 0) {
        $script:nativeFailure = $code
        throw 'VsDevCmd initialization failed.'
    }
}

function Test-X64Pe([string]$Path) {
    $reader = [IO.BinaryReader]::new([IO.File]::OpenRead($Path))
    try {
        if ($reader.BaseStream.Length -lt 64 -or $reader.ReadUInt16() -ne 0x5A4D) { throw "Not a PE file: $Path" }
        $reader.BaseStream.Position = 60
        $offset = $reader.ReadUInt32()
        if ($offset -gt ($reader.BaseStream.Length - 26)) { throw "Invalid PE offset: $Path" }
        $reader.BaseStream.Position = $offset
        if ($reader.ReadUInt32() -ne 0x4550 -or $reader.ReadUInt16() -ne 0x8664) { throw "Expected an x64 PE file: $Path" }
        $reader.BaseStream.Position = $offset + 24
        if ($reader.ReadUInt16() -ne 0x20B) { throw "Expected PE32+: $Path" }
    } finally { $reader.Dispose() }
}

function Assert-NoReparsePoint([string]$Path) {
    $item = Get-Item -LiteralPath $Path -Force -ErrorAction SilentlyContinue
    if (-not $item) { return }
    if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw "Clean refuses a reparse point: $Path" }
}

function Assert-CleanTree([string]$Path) {
    Assert-NoReparsePoint $Path
    if (Test-Path -LiteralPath $Path -PathType Container) {
        foreach ($item in Get-ChildItem -LiteralPath $Path -Force) { Assert-CleanTree $item.FullName }
    }
}

try {
    if ($args.Count -gt 1 -or $command -notin @('help','clean','doctor','debug','release','test','package','release-all')) {
        Write-Line '  [FAIL] Unknown command or extra arguments. Run build.bat help.' Red
        exit 2
    }
    if ($command -notin @('help','clean')) {
        $logDir = Join-Path $root 'logs\build'
        [void][IO.Directory]::CreateDirectory($logDir)
        $log = Join-Path $logDir ('build-{0:yyyyMMdd-HHmmss}-{1}.log' -f [datetime]::UtcNow, $PID)
        [IO.File]::WriteAllText($log, '', [Text.UTF8Encoding]::new($true))
    }
    Write-Line '+================================================================+' Cyan
    Write-Line '|  CAMPUSLINK // BUILD & RELEASE CONSOLE                          |' Cyan
    Write-Line '|  Windows x64    /   C++20                                      |' Cyan
    Write-Line '+================================================================+' Cyan
    if ($command -eq 'help') {
        Write-Line '  debug | release (default)  Compile, link and stage the SDL3 runtime.'
        Write-Line '  test                       Build Debug and run core, SDL and ImGui tests.'
        Write-Line '  doctor                     Check the Windows toolchain and dependencies.'
        Write-Line '  clean                      Remove build/obj and build/bin; preserve logs.'
        Write-Line '  package                     Build/test Release, verify and write a portable ZIP.'
        Write-Line '  release-all                 Verify Debug and Release, create a local versioned release.'
        Write-Line '  Exit: 0 pass; 1 validation error; 2 usage; native failures preserve their code.'
        Write-Line '  CAMPUSLINK_ASCII=1          Keep the console code page and use ASCII marks.'
        Write-Line '  NO_COLOR=1                  Disable color.'
        exit 0
    }
    if ($command -eq 'clean') {
        Assert-NoReparsePoint $build
        $paths = @((Join-Path $build 'obj'), (Join-Path $build 'bin'))
        foreach ($path in $paths) { Assert-CleanTree $path }
        foreach ($path in $paths) {
            if (Test-Path -LiteralPath $path) { Remove-Item -LiteralPath $path -Recurse -Force }
        }
        Write-Line '  [OK] Clean complete. Logs, dependencies and reports preserved.' Green
        exit 0
    }
    Write-Line ('  Run: {0} | UTC: {1:o}' -f $command, [datetime]::UtcNow)
    Write-Line "  Log: $log"

    $version = (Get-Content -LiteralPath (Join-Path $root 'VERSION') -Raw).Trim()
    if ($version -notmatch '\A\d+\.\d+\.\d+\z') { throw 'VERSION must contain a numeric major.minor.patch version.' }
    if ($command -eq 'release-all' -and (Test-Path -LiteralPath (Join-Path $root ('dist\releases\v' + $version)))) {
        throw 'Versioned local release already exists. Preserve or move it before rebuilding.'
    }
    $configs = if ($command -eq 'release-all') { @('debug','release') } elseif ($command -in @('debug','test')) { @('debug') } else { @('release') }
    $withTests = $command -in @('test','package','release-all')
    $tools = @{}
    $deps = @{}
    Write-Line "  Version: $version | Target: x86_64-pc-windows-msvc"
    Invoke-Step 1 {
        if ($env:PROCESSOR_ARCHITECTURE -ne 'AMD64' -or $env:PROCESSOR_ARCHITEW6432 -or -not [Environment]::Is64BitProcess) {
            throw 'Requires native x64 Windows and a 64-bit PowerShell process.'
        }
        $os = Get-CimInstance Win32_OperatingSystem
        if ($os.ProductType -ne 1 -or [int]$os.BuildNumber -lt 22000) {
            if ($env:GITHUB_ACTIONS -ne 'true') { throw 'Windows 11 client is required (build >= 22000).' }
            Write-Line '    [WARN] Hosted Windows runner is not a Windows 11 client validation.' Yellow
        }
        Write-Line "    $($os.Caption) | $($os.Version) | build $($os.BuildNumber)"
        if ($env:VSCMD_ARG_TGT_ARCH -notin @('x64','amd64') -or -not $env:VCToolsInstallDir -or -not $env:WindowsSDKVersion) {
            Import-VsEnvironment
        }
        if ($env:VSCMD_ARG_TGT_ARCH -notin @('x64','amd64') -or -not $env:VCToolsInstallDir -or
            -not $env:WindowsSdkDir -or -not $env:WindowsSDKVersion -or -not $env:INCLUDE -or -not $env:LIB) {
            throw 'Incomplete VS x64 environment. Install VC x64 tools and the Windows 11 SDK.'
        }
        $sdk = $env:WindowsSDKVersion.TrimEnd('\')
        if ($sdk -notmatch '^10\.0\.\d+\.\d+$' -or ([version]$sdk).Build -lt 22000) { throw 'Select a Windows 11 SDK (10.0.22000.0 or newer).' }
        Require-Files @((Join-Path $env:VCToolsInstallDir 'include\vector'), (Join-Path $env:VCToolsInstallDir 'lib\x64\libcmt.lib'),
            (Join-Path $env:VCToolsInstallDir 'lib\x64\libcmtd.lib'), (Join-Path $env:WindowsSdkDir "Include\$sdk\um\Windows.h"),
            (Join-Path $env:WindowsSdkDir "Include\$sdk\ucrt\stdio.h"), (Join-Path $env:WindowsSdkDir "Lib\$sdk\um\x64\kernel32.lib"),
            (Join-Path $env:WindowsSdkDir "Lib\$sdk\ucrt\x64\libucrt.lib"))
        $pinnedCompiler = Join-Path $root 'third_party\llvm\bin\clang++.exe'
        $tools.compiler = if (Test-Path -LiteralPath $pinnedCompiler -PathType Leaf) { $pinnedCompiler } else {
            Find-Tool 'clang++.exe' @("$env:ProgramFiles\LLVM\bin\clang++.exe")
        }
        $tools.netLibraries = @('wlanapi','iphlpapi','ws2_32','setupapi','ole32','advapi32')
        Require-Files @($tools.netLibraries | ForEach-Object { Join-Path $env:WindowsSdkDir "Lib\$sdk\um\x64\$_.lib" })
        if (-not $tools.compiler) { throw 'LLVM clang++.exe not found. Install LLVM x64 and add its bin directory to PATH.' }
        $llvm = Split-Path -Parent $tools.compiler
        $lld = Join-Path $llvm 'lld-link.exe'
        $tools.linker = if (Test-Path -LiteralPath $lld -PathType Leaf) { $lld } else { Find-Tool 'lld-link.exe' }
        if ($tools.linker) {
            $tools.linkerChoice = '-fuse-ld=lld'
            $tools.linkerSearch = @('-B', (Split-Path -Parent $tools.linker))
        } else {
            $tools.linker = Join-Path $env:VCToolsInstallDir 'bin\Hostx64\x64\link.exe'
            Require-Files @($tools.linker)
            $tools.linkerChoice = '-fuse-ld=link'
            $tools.linkerSearch = @()
        }
        Write-Line "    Windows SDK $sdk | VC toolset $env:VCToolsVersion | linker $($tools.linker)"
        Invoke-Native $tools.compiler @('--version')
        if ((Split-Path -Leaf $tools.linker) -ieq 'lld-link.exe') { Invoke-Native $tools.linker @('--version') }
        else { Write-Line "    link.exe $((Get-Item -LiteralPath $tools.linker).VersionInfo.FileVersion)" }
    }
    Invoke-Step 2 {
        $lock = Get-Content -LiteralPath (Join-Path $root 'deps.lock') -Raw | ConvertFrom-Json
        $sdl = Join-Path $root 'third_party\sdl3'
        $deps.headers = Join-Path $sdl 'include'
        $deps.lib = Join-Path $sdl 'lib\x64\SDL3.lib'
        $deps.dll = Join-Path $sdl 'lib\x64\SDL3.dll'
        $header = Join-Path $deps.headers 'SDL3\SDL_version.h'
        $deps.imgui = Join-Path $root 'third_party\imgui'
        $deps.imguiBackends = Join-Path $deps.imgui 'backends'
        $imguiHeader = Join-Path $deps.imgui 'imgui.h'
        $deps.vendor = @('imgui.cpp','imgui_draw.cpp','imgui_tables.cpp','imgui_widgets.cpp',
            'backends\imgui_impl_sdl3.cpp','backends\imgui_impl_sdlgpu3.cpp','backends\imgui_impl_sdlrenderer3.cpp')
        Require-Files (@($imguiHeader) + @($deps.vendor | ForEach-Object { Join-Path $deps.imgui $_ }))
        if ((Get-Content -LiteralPath $imguiHeader -Raw) -notmatch ('(?m)^#define\s+IMGUI_VERSION\s+"' + [regex]::Escape($lock.imgui.version) + '"')) {
            throw "Expected Dear ImGui $($lock.imgui.version)"
        }
        Write-Line "    Dear ImGui $($lock.imgui.version) under $($deps.imgui)"
        if ($env:VULKAN_SDK -and (Test-Path -LiteralPath "$env:VULKAN_SDK\Include\vulkan\vulkan.h" -PathType Leaf)) {
            Write-Line "    Optional Vulkan SDK found at $env:VULKAN_SDK"
        }
        if (Test-Path -LiteralPath "$env:SystemRoot\System32\vulkan-1.dll" -PathType Leaf) {
            Write-Line '    Vulkan loader file found; physical device not checked by doctor.'
        } else { Write-Line '    [WARN] Vulkan loader not found; application can use SDL software rendering.' Yellow }
        try { Require-Files @($header, (Join-Path $deps.headers 'SDL3\SDL.h'), (Join-Path $deps.headers 'SDL3\SDL_main.h'), $deps.lib, $deps.dll) }
        catch { throw "$($_.Exception.Message). Extract the locked SDL3 VC ZIP as documented in README.md." }
        $headers = Get-Content -LiteralPath $header -Raw
        $parts = foreach ($part in @('MAJOR','MINOR','MICRO')) {
            if ($headers -notmatch "(?m)^\s*#define\s+SDL_${part}_VERSION\s+(\d+)\b") { throw 'Could not read SDL header version.' }
            $Matches[1]
        }
        if (($parts -join '.') -ne $lock.sdl3.version) { throw "Expected SDL $($lock.sdl3.version), found $($parts -join '.')." }
        Test-X64Pe $deps.dll
        Write-Line "    SDL $($lock.sdl3.version) headers + PE32+ AMD64 DLL; import library checked by linker."
    }
    if ($command -ne 'doctor') {
        $target = '--target=x86_64-pc-windows-msvc'
        $buildRevision = & git.exe -C $root rev-parse '--short=12' HEAD
        if ($LASTEXITCODE -ne 0 -or $buildRevision -notmatch '^[0-9a-f]{12}$') { throw 'Unable to identify the build commit' }
        $fontRoot = Join-Path $root 'assets\fonts'
        $fontLock = (Get-Content -LiteralPath (Join-Path $root 'deps.lock') -Raw | ConvertFrom-Json).fonts
        foreach ($font in $fontLock) {
            foreach ($item in @(@{ file = $font.file; sha = $font.sha256 }, @{ file = $font.licenseFile; sha = $font.licenseSha256 })) {
                if ((Get-FileHash -LiteralPath (Join-Path $fontRoot $item.file) -Algorithm SHA256).Hash.ToLowerInvariant() -cne $item.sha) { throw "Font integrity mismatch: $($item.file)" }
            }
        }
        $core = @('src\simulation\lab.cpp','src\network\diagnostics.cpp','src\network\dns.cpp','src\performance\score.cpp')
        $app = @('src\main.cpp','src\ui\desktop.cpp','src\platform\windows\native.cpp')
        $builds = @{}
        $objRoot = Join-Path $build 'obj'
        $binRoot = Join-Path $build 'bin'
        if ($command -eq 'release-all') {
            Assert-NoReparsePoint $build
            Assert-NoReparsePoint $objRoot
            Assert-NoReparsePoint $binRoot
            $targets = @((Join-Path $objRoot 'debug'), (Join-Path $objRoot 'release'), (Join-Path $binRoot 'debug'), (Join-Path $binRoot 'release'))
            foreach ($dir in $targets) { Assert-CleanTree $dir }
            foreach ($dir in $targets) { if (Test-Path -LiteralPath $dir) { Remove-Item -LiteralPath $dir -Recurse -Force } }
        }
        Invoke-Step 3 {
            foreach ($config in $configs) {
                $obj = Join-Path $objRoot $config
                $bin = Join-Path $binRoot $config
                [void][IO.Directory]::CreateDirectory($obj)
                $runtime = if ($config -eq 'debug') { '-fms-runtime-lib=static_dbg' } else { '-fms-runtime-lib=static' }
                $opts = if ($config -eq 'debug') { @('-O0','-g','-gcodeview') } else { @('-O2','-DNDEBUG') }
                $common = @($target, $runtime, '-std=c++20', '-DUNICODE', '-D_UNICODE', '-DWINVER=0x0A00', '-D_WIN32_WINNT=0x0A00',
                    "-DCAMPUSLINK_VERSION=$version", "-DCAMPUSLINK_BUILD=$buildRevision", '-I', (Join-Path $root 'include'), '-isystem', $deps.headers,
                    '-isystem', $deps.imgui, '-isystem', $deps.imguiBackends) + $opts
                $flags = @('-Wall', '-Wextra', '-Wpedantic', '-Werror')
                $coreObjects = @()
                $appObjects = @()
                foreach ($source in ($core + $app)) {
                    $object = Join-Path $obj (([IO.Path]::GetFileNameWithoutExtension($source)) + '.obj')
                    Invoke-Native $tools.compiler ($common + $flags + @('-c', (Join-Path $root $source), '-o', $object))
                    if ($source -in $core) { $coreObjects += $object } else { $appObjects += $object }
                }
                foreach ($source in $deps.vendor) {
                    $object = Join-Path $obj (([IO.Path]::GetFileNameWithoutExtension($source)) + '.obj')
                    Invoke-Native $tools.compiler ($common + @('-c', (Join-Path $deps.imgui $source), '-o', $object))
                    $appObjects += $object
                }
                $testObject = Join-Path $obj 'core-tests.obj'
                if ($withTests) {
                    Invoke-Native $tools.compiler ($common + $flags + @('-c', (Join-Path $root 'tests\unit\core.cpp'), '-o', $testObject))
                }
                $builds[$config] = @{ obj = $obj; bin = $bin; appObjects = $appObjects; coreObjects = $coreObjects; testObject = $testObject }
            }
        }
        Invoke-Step 4 {
            $link = @($target, '-nostartfiles', $tools.linkerChoice, '-Xlinker', '/SUBSYSTEM:CONSOLE', '-lkernel32', '-loldnames') + $tools.linkerSearch
            foreach ($config in $configs) {
                $paths = $builds[$config]
                $pendingExe = Join-Path $paths.obj 'CampusLink.exe'
                $debugLink = if ($config -eq 'debug') { @('-Xlinker', '/DEBUG', '-Xlinker', ('/PDB:' + (Join-Path $paths.obj 'CampusLink.pdb'))) } else { @() }
                $netLibs = @($tools.netLibraries | ForEach-Object { '-l' + $_ })
                # The object directives choose libcmt or libcmtd; do not let the driver inject the release CRT.
                Invoke-Native $tools.compiler ($link + $paths.coreObjects + $paths.appObjects + @($deps.lib, '-o', $pendingExe) + $netLibs + $debugLink)
                Test-X64Pe $pendingExe
                [void][IO.Directory]::CreateDirectory($paths.bin)
                Copy-Item -LiteralPath $deps.dll -Destination (Join-Path $paths.bin 'SDL3.dll') -Force
                $fontDest = Join-Path $paths.bin 'assets\fonts'
                [void][IO.Directory]::CreateDirectory($fontDest)
                foreach ($font in $fontLock) { Copy-Item -LiteralPath (Join-Path $fontRoot $font.file) -Destination $fontDest -Force }
                Copy-Item -LiteralPath $pendingExe -Destination (Join-Path $paths.bin 'CampusLink.exe') -Force
                if ($withTests) {
                    $testExe = Join-Path $paths.bin 'CampusLinkTests.exe'
                    Invoke-Native $tools.compiler ($link + $paths.coreObjects + @($paths.testObject, '-o', $testExe))
                    Test-X64Pe $testExe
                }
                Write-Line "    Output ($config): $($paths.bin)"
            }
        }
        if ($withTests) {
            Invoke-Step 5 {
                foreach ($config in $configs) {
                    $bin = $builds[$config].bin
                    Invoke-Native (Join-Path $bin 'CampusLinkTests.exe') @()
                    $exe = Join-Path $bin 'CampusLink.exe'
                    Invoke-Native $exe @('--self-test')
                    Invoke-Native $exe @('--ui-smoke')
                    Invoke-Native $exe @('--inventory-smoke')
                    Invoke-Native $exe @('--profile')
                }
            }
        }
        if ($command -in @('package','release-all')) {
            $delivery = @{}
            Invoke-Step 6 {
                $launcher = Join-Path $PSScriptRoot 'package.ps1'
                $params = @{ Root = $root; Version = $version; BinaryDirectory = $builds['release'].bin;
                    Compiler = $tools.compiler; BuildLog = $log; ReleaseAll = ($command -eq 'release-all') }
                & $launcher @params | ForEach-Object { Write-Line ('    ' + $_) }
                $delivery.path = Join-Path $root ('dist\packages\CAMPUSLINK-' + $version + '-windows-x64-portable.zip')
            }
            Invoke-Step 7 {
                & (Join-Path $PSScriptRoot 'verify-release.ps1') -Archive $delivery.path | ForEach-Object { Write-Line ('    ' + $_) }
                if ($command -eq 'release-all') {
                    $folder = Join-Path $root ('dist\releases\v' + $version)
                    & (Join-Path $PSScriptRoot 'verify-release.ps1') -Archive (Join-Path $folder ([IO.Path]::GetFileName($delivery.path))) -ReleaseFolder $folder |
                        ForEach-Object { Write-Line ('    ' + $_) }
                }
            }
        }
    }
} catch {
    $exitCode = if ($nativeFailure) { $nativeFailure } else { 1 }
    Write-Line "  [FAIL] $($_.Exception.Message)" Red
} finally {
    if ($log) {
        Write-Line '  ---------------------------------------------------------------' Cyan
        for ($i = 0; $i -lt $stages.Count; $i++) {
            $status = $results[$i]
            $color = switch ($status) {
                'PASS' { 'Green' }
                'FAIL' { 'Red' }
                default { 'DarkGray' }
            }
            Write-Line ('    [{0:00}/07] {1,-31} {2}' -f ($i + 1), $stages[$i], $status) $color
        }
        Write-Line "  Command exit: $exitCode | Log: $log"
    }
}
exit $exitCode
