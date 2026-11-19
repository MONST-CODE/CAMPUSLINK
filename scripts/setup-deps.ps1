param([switch]$InstallPinnedLLVM)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$lock = Get-Content -LiteralPath (Join-Path $root 'deps.lock') -Raw | ConvertFrom-Json
$third = Join-Path $root 'third_party'
$temp = Join-Path ([IO.Path]::GetTempPath()) ('CampusLink deps ' + [guid]::NewGuid())
[void][IO.Directory]::CreateDirectory($temp)
[void][IO.Directory]::CreateDirectory($third)

function Get-Verified([string]$Url, [string]$Hash, [string]$Destination) {
    Invoke-WebRequest -UseBasicParsing -Uri $Url -OutFile $Destination
    $actual = (Get-FileHash -LiteralPath $Destination -Algorithm SHA256).Hash
    if ($actual -ne $Hash) { throw "Dependency SHA-256 mismatch: $Url" }
    Write-Host "Verified SHA-256 $actual : $Url"
}
try {
    # Never overwrite existing third-party installations silently.
    foreach ($name in @('sdl3','imgui')) {
        if (Test-Path -LiteralPath (Join-Path $third $name)) { throw "third_party/$name already exists. Validate or move it aside explicitly." }
    }
    $sdlZip = Join-Path $temp 'sdl.zip'
    $imguiTar = Join-Path $temp 'imgui.tar.gz'
    Get-Verified $lock.sdl3.windowsVcUrl $lock.sdl3.windowsVcSha256 $sdlZip
    Get-Verified $lock.imgui.sourceUrl $lock.imgui.sourceSha256 $imguiTar
    Expand-Archive -LiteralPath $sdlZip -DestinationPath $temp
    & "$env:SystemRoot\System32\tar.exe" -xzf $imguiTar -C $temp
    if ($LASTEXITCODE -ne 0) { throw "tar.exe returned $LASTEXITCODE" }
    $sdl = Join-Path $temp ('SDL3-' + $lock.sdl3.version)
    $imgui = Join-Path $temp ('imgui-' + $lock.imgui.version)
    if (-not (Test-Path -LiteralPath (Join-Path $sdl 'lib\x64\SDL3.dll')) -or
        -not (Test-Path -LiteralPath (Join-Path $imgui 'backends\imgui_impl_sdlgpu3.cpp'))) { throw 'Unexpected dependency archive layout' }
    Move-Item -LiteralPath $sdl -Destination (Join-Path $third 'sdl3')
    Move-Item -LiteralPath $imgui -Destination (Join-Path $third 'imgui')
    if ($InstallPinnedLLVM) {
        $destination = Join-Path $third 'llvm'
        if (Test-Path -LiteralPath $destination) { throw 'third_party/llvm already exists.' }
        $exe = Join-Path $temp 'llvm.exe'
        Get-Verified $lock.llvm.windowsInstallerUrl $lock.llvm.windowsInstallerSha256 $exe
        # NSIS requires the /D= path last, without quoting the argument itself.
        $installer = Start-Process -FilePath $exe -ArgumentList "/S /D=$destination" -Wait -PassThru
        if ($installer.ExitCode -ne 0 -or -not (Test-Path -LiteralPath (Join-Path $destination 'bin\clang++.exe'))) {
            throw "LLVM installation failed with code $($installer.ExitCode)"
        }
    }
} finally {
    Remove-Item -LiteralPath $temp -Recurse -Force
}
