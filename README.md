# CAMPUSLINK

Assistant de connexion Wi-Fi pour **Windows 11 x64**. À l’accueil, choisissez **Me connecter à un réseau**, sélectionnez un point d’accès et lancez la **Connexion normale** ou la **Connexion assistée directe**. L’assistance ne requiert ni essai normal préalable ni configuration de laboratoire. Windows conserve la gestion des identifiants, du portail et des adresses IP.

**EN.** CAMPUSLINK is a native Wi-Fi connection assistant. From Home, choose **Connect to Wi-Fi**, select an access point, and choose normal or direct assisted connection. Extract the whole portable ZIP and run `CampusLink.exe` next to `SDL3.dll`. Vulkan is preferred; SDL software rendering is the fallback.

## Build

Use LLVM and Visual Studio C++ Build Tools with a Windows 11 SDK, then prepare the hash-locked dependencies:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts\setup-deps.ps1 -InstallPinnedLLVM
.\build.bat doctor
.\build.bat test
.\build.bat release-all
```

Build logs: `logs/build/`. Portable ZIP: `dist/packages/`. [Build](docs/BUILD_WINDOWS.md) · [Distribution and checksums](docs/BUILD_RELEASE.md).

## Advanced tools

Scoped CLP1 measurements require an explicitly authorized endpoint and profile/BSSID. The Virtual Client Lab and the multipath scheduler lab run offline. Neither simulates real radio capability, and neither forwards Internet traffic. See the [FR/EN user guide](docs/USER_GUIDE.md), [architecture](docs/ARCHITECTURE.md), [transport decision](docs/decisions/0002-connectivity-evidence.md) and [Windows 11 field validation](docs/WINDOWS11_VALIDATION.md).
