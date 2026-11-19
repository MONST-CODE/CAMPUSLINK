# CAMPUSLINK portable — Windows x64

Extract the full archive to a folder you can write to. Run `CampusLink.exe` next to `SDL3.dll` on Windows 11 x64. No administrator account is required for the offline lab. A Vulkan driver is optional: the application falls back to SDL software rendering when Vulkan cannot start.

1. Open **Virtual Client Lab** to run a deterministic offline scenario. All client metrics there are simulated.
2. Use **Hardware Lab** to inspect driver-reported information and **Network Discovery** to request a WLAN scan. Windows location policy can limit the result.
3. To run a real experiment, read [USER_GUIDE.md](USER_GUIDE.md) and prepare your own authorized `test_scope.conf` from `config/test_scope.example.conf`. No experiment is active by default. See [network diagnostics](NETWORK_DIAGNOSTICS.md) and [Windows 11 verification](WINDOWS11_VALIDATION.md).

[Build information](BUILD_INFO.json) · [Runtime imports](RUNTIME_IMPORTS.txt) · [Release notes](RELEASE_NOTES.md) · [Third-party notices](THIRD_PARTY_NOTICES.md)
