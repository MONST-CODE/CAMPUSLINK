<#
.SYNOPSIS
  Developer build with MSYS2 UCRT64 clang (x86_64-w64-windows-gnu).

.DESCRIPTION
  For local iteration, offscreen UI captures and unit tests on a machine without
  Visual Studio. It is NOT the release toolchain: release packages are produced by
  build.bat (clang --target=x86_64-pc-windows-msvc, pinned SDL VC DLL) in CI.
  The same warning flags as the release build are enforced.

  Requirements: MSYS2 with mingw-w64-ucrt-x86_64-clang and mingw-w64-ucrt-x86_64-sdl3,
  and Dear ImGui sources (version in deps.lock) under third_party\imgui.

.EXAMPLE
  powershell -File scripts\dev-mingw.ps1            # build app + tests, run tests
  powershell -File scripts\dev-mingw.ps1 -Release   # optimized build
#>
param(
    [switch] $Release,
    [switch] $NoTests,
    [string] $Msys = $(if ($env:MSYS2_UCRT64) { $env:MSYS2_UCRT64 } else { 'C:\msys64\ucrt64' })
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$version = (Get-Content -LiteralPath (Join-Path $root 'VERSION') -Raw).Trim()
$config = if ($Release) { 'release' } else { 'debug' }
$obj = Join-Path $root "build\dev\obj\$config"
$bin = Join-Path $root "build\dev\bin\$config"
$clang = Join-Path $Msys 'bin\clang++.exe'
$imgui = Join-Path $root 'third_party\imgui'
foreach ($required in @($clang, (Join-Path $Msys 'include\SDL3\SDL.h'), (Join-Path $Msys 'bin\SDL3.dll'), (Join-Path $imgui 'imgui.h'))) {
    if (-not (Test-Path -LiteralPath $required)) { throw "Missing: $required" }
}
$lock = Get-Content -LiteralPath (Join-Path $root 'deps.lock') -Raw | ConvertFrom-Json
if ((Get-Content -LiteralPath (Join-Path $imgui 'imgui.h') -Raw) -notmatch ('#define\s+IMGUI_VERSION\s+"' + [regex]::Escape($lock.imgui.version) + '"')) {
    throw "Expected Dear ImGui $($lock.imgui.version) in $imgui"
}
$revision = (& git.exe -C $root rev-parse --short=12 HEAD 2>$null)
if ($LASTEXITCODE -ne 0 -or -not $revision) { $revision = 'devbuild0000' }
[void][IO.Directory]::CreateDirectory($obj); [void][IO.Directory]::CreateDirectory($bin)
$env:PATH = (Join-Path $Msys 'bin') + ';' + $env:PATH

$opt = if ($Release) { @('-O2', '-DNDEBUG') } else { @('-O0', '-g') }
$common = @('-std=c++20', '-DUNICODE', '-D_UNICODE', '-DWINVER=0x0A00', '-D_WIN32_WINNT=0x0A00',
    "-DCAMPUSLINK_VERSION=$version", "-DCAMPUSLINK_BUILD=$revision", '-I', (Join-Path $root 'include'),
    '-isystem', $imgui, '-isystem', (Join-Path $imgui 'backends')) + $opt
$strict = @('-Wall', '-Wextra', '-Wpedantic', '-Werror')
$core = @('src\simulation\lab.cpp', 'src\network\diagnostics.cpp', 'src\network\dns.cpp', 'src\performance\score.cpp', 'src\performance\path_lab.cpp')
$app = @('src\main.cpp', 'src\ui\desktop.cpp', 'src\platform\windows\native.cpp')
$vendor = @('imgui.cpp', 'imgui_draw.cpp', 'imgui_tables.cpp', 'imgui_widgets.cpp',
    'backends\imgui_impl_sdl3.cpp', 'backends\imgui_impl_sdlgpu3.cpp', 'backends\imgui_impl_sdlrenderer3.cpp')
$headers = Get-ChildItem -LiteralPath (Join-Path $root 'include'), (Join-Path $root 'src') -Recurse -Include *.hpp, *.inc
$newestHeader = ($headers | Measure-Object -Property LastWriteTimeUtc -Maximum).Maximum

function Build-Object([string] $source, [string] $name, [string[]] $extra) {
    $object = Join-Path $obj ($name + '.o')
    $src = Get-Item -LiteralPath $source
    if ((Test-Path -LiteralPath $object) -and (Get-Item -LiteralPath $object).LastWriteTimeUtc -gt $src.LastWriteTimeUtc -and
        ($extra.Count -eq 0 -or (Get-Item -LiteralPath $object).LastWriteTimeUtc -gt $newestHeader)) { return $object }
    Write-Host "  CXX $name"
    & $clang @common @extra -c $source -o $object
    if ($LASTEXITCODE -ne 0) { throw "Compilation failed: $source" }
    return $object
}
$sw = [Diagnostics.Stopwatch]::StartNew()
$coreObjects = foreach ($s in $core) { Build-Object (Join-Path $root $s) ([IO.Path]::GetFileNameWithoutExtension($s)) $strict }
$appObjects = foreach ($s in $app) { Build-Object (Join-Path $root $s) ([IO.Path]::GetFileNameWithoutExtension($s)) $strict }
$vendorObjects = foreach ($s in $vendor) { Build-Object (Join-Path $imgui $s) ('imgui-' + [IO.Path]::GetFileNameWithoutExtension($s)) @() }
$libs = @('-lSDL3', '-lwlanapi', '-liphlpapi', '-lws2_32', '-lsetupapi', '-lole32', '-loleaut32', '-luuid', '-ladvapi32', '-lshell32', '-luser32', '-limm32')
$static = @('-static-libgcc', '-static-libstdc++', '-Wl,-Bstatic', '-lwinpthread', '-Wl,-Bdynamic')
Write-Host '  LINK CampusLink.exe'
# UNICODE makes SDL_main provide wWinMain; -municode selects that CRT entry. Console subsystem
# keeps --version/--self-test output visible, as in the release link (/SUBSYSTEM:CONSOLE).
& $clang @coreObjects @appObjects @vendorObjects -municode -mconsole -o (Join-Path $bin 'CampusLink.exe') -L (Join-Path $Msys 'lib') @libs @static
if ($LASTEXITCODE -ne 0) { throw 'Link failed' }
Copy-Item -LiteralPath (Join-Path $Msys 'bin\SDL3.dll') -Destination $bin -Force
$fonts = Join-Path $bin 'assets\fonts'; [void][IO.Directory]::CreateDirectory($fonts)
Copy-Item -Path (Join-Path $root 'assets\fonts\*.ttf') -Destination $fonts -Force
if (-not $NoTests) {
    $testObject = Build-Object (Join-Path $root 'tests\unit\core.cpp') 'core-tests' $strict
    & $clang @coreObjects $testObject -o (Join-Path $bin 'CampusLinkTests.exe') @static
    if ($LASTEXITCODE -ne 0) { throw 'Test link failed' }
    & (Join-Path $bin 'CampusLinkTests.exe')
    if ($LASTEXITCODE -ne 0) { throw 'Unit tests failed' }
}
Write-Host ("Built {0} ({1}) in {2:n1} s -> {3}" -f $version, $config, $sw.Elapsed.TotalSeconds, $bin)
