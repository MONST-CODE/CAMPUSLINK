# Third-party notices

- SDL3 3.2.30 — SDL contributors, zlib license, bundled dynamic library. The official VC x64 archive retains `LICENSE.txt`.
- Dear ImGui 1.92.5 — Omar Cornut and contributors, MIT license. The core and SDL backends are compiled into the application. The official source license is bundled.
- LLVM — Apache-2.0 with LLVM exception. Compiler only; installer not distributed in the portable ZIP. MSVC STL/CRT and Windows SDK are provided by the Microsoft toolchain. No compiler/SDK DLL is added by the packaging script.
- Vulkan is loaded dynamically by SDL where available. No Vulkan SDK redistributable is bundled.

Project source licensing is not changed by this delivery. This local build is not an authorization to redistribute project sources or binaries. Refer to `deps.lock` for exact official sources and checksums.

## UI fonts

- **Inter v4.1**, Copyright (c) 2016 The Inter Project Authors. SIL Open Font License 1.1. Upstream: <https://github.com/rsms/inter>.
- **JetBrains Mono v2.304**, Copyright 2020 The JetBrains Mono Project Authors. SIL Open Font License 1.1. Upstream: <https://github.com/JetBrains/JetBrainsMono>.

The unmodified font binaries and upstream notices are pinned in `deps.lock`. Full notices are under `assets/fonts/` in source and `LICENSES/` in the portable ZIP.
