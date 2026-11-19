# Build on Windows 11 x64

Install Visual Studio Build Tools with the Desktop C++ workload and a Windows SDK >= 10.0.22000.0. `vswhere` discovers the toolchain and the wrapper initializes an x64 environment if necessary. On a clean checkout, from PowerShell run:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts\setup-deps.ps1 -InstallPinnedLLVM
```

Omit `-InstallPinnedLLVM` if using a compatible installed LLVM. The dependency script downloads from the official sources and verifies the hashes in `deps.lock` before extraction or installation. It does not replace existing dependencies. Installation of the Microsoft toolset is external to `build.bat`.

```bat
build.bat doctor
build.bat debug
build.bat release
build.bat test
build.bat package
build.bat release-all
```

Builds use direct `clang++` commands with ABI `x86_64-pc-windows-msvc` and LLD or MSVC COFF linker. No generator is used for CAMPUSLINK. The Vulkan SDK is optional: SDL3 loads Vulkan dynamically and ImGui's SDL GPU backend embeds shaders. `doctor` prints what it finds without claiming successful Vulkan presentation. Logs: `logs/build/`. Set `CAMPUSLINK_ASCII=1` or `NO_COLOR=1` as needed. `clean` preserves archives and logs. The hosted workflow is Windows Server, and its Windows build results do not certify Windows 11 client or Dell compatibility.
