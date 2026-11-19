# CAMPUSLINK

CAMPUSLINK is a Windows x64 Wi-Fi research desktop. It keeps two activities separate: **Virtual Client Lab** simulates logical clients and DHCP entirely offline; **Physical Experiment** uses the host's real Windows WLAN profile only after the operator authorizes an exact test scope. Simulated clients are not radios, and no lease is transferred to the host.

## Install and run

Extract the verified portable ZIP and run `CampusLink.exe` next to `SDL3.dll`. To build from source, install LLVM and Visual Studio C++ Build Tools with a Windows 11 SDK, then prepare the locked dependencies:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts\setup-deps.ps1 -InstallPinnedLLVM
.\build.bat doctor
.\build.bat test
.\build.bat release-all
```

Build logs are under `logs/build/`. The portable ZIP is written to `dist/packages/`; `release-all` also creates a local versioned release. [Build and validation instructions](docs/BUILD_WINDOWS.md) · [Package layout and checksums](docs/BUILD_RELEASE.md).

## Use

Start with **Virtual Client Lab** for deterministic offline scenarios. **Hardware Lab** and **Network Discovery** report driver data and unknowns explicitly. To run a real experiment, read the [user guide](docs/USER_GUIDE.md) and configure an authorized private CLP1 endpoint and Windows profile. Vulkan is preferred; SDL software rendering is used if unavailable.

[Architecture](docs/ARCHITECTURE.md) · [Test plan](docs/TEST_PLAN.md) · [Known limitations](docs/KNOWN_LIMITATIONS.md) · [Delivery evidence](docs/reports/FINAL_DELIVERY_REPORT.md) · [Original specification](docs/CAMPUSLINK_SPEC_ARENA.md)

### Candidate 0.2.0

The portable package targets Windows 11 x64. The integrated Lab runs offline; its client count and RSS measurements describe simulation only. Physical trials require exact profile/BSSID and an authorized endpoint. See [Windows 11 acceptance](docs/WINDOWS11_VALIDATION.md) for client, Dell/AX201, Vulkan and WLAN field checks; Windows Server CI does not replace those checks.
