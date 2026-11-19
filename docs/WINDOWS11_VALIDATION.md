# Windows 11 x64 and Dell Precision 7750 acceptance

Use a clean, interactive Windows 11 x64 client. For the Dell reference, use a physical Precision 7750 with an Intel AX201. Neither Windows Server CI nor a simulated client substitutes for this run.

1. Download the portable ZIP, `SHA256SUMS.txt`, and external `manifest.json` from the same build into one directory. Verify the ZIP SHA-256 against the published checksum before extracting or running the application. For the checker, use the version-matched `scripts/` from the source checkout, or the `validation/` scripts in the verified ZIP.
2. Open Windows PowerShell and run the following. Omit `-RequireDellAx201` on another workstation and `-TryVulkan` if testing only the software fallback. A requested Vulkan test fails if the native Vulkan backend cannot render; software rendering does not count as Vulkan evidence.

```powershell
$release = 'C:\QA\CAMPUSLINK 0.2.0'
.\scripts\validate-win11.ps1 -Zip "$release\CAMPUSLINK-0.2.0-windows-x64-portable.zip" `
  -ReleaseFolder $release -OutputDirectory 'C:\QA\CampusLink evidence' -RequireDellAx201 -TryVulkan
```

3. Save `results.json`, `machine.json`, and logs. Redact local identifiers and network details before sharing. A matching model and signed driver is inventory evidence only, not evidence of RF throughput or simultaneous STA. Run `CampusLink.exe` normally and check visible rendering, focus, high DPI, navigation, and the software fallback. `--ui-smoke` renders every panel with SDL dummy video; it does not check human interaction.
4. If authorized, run field checks on isolated lab APs with saved Windows profiles, and retain a way to recover the connection locally. In Hardware, record driver-reported PHY/roles and active association separately. In Discovery, test with WLAN/location unavailable and enabled; record native errors and make sure the UI does not suggest bypassing OS consent. In Physical Experiment, load a scope for each authorized profile/BSSID and a controlled CLP1 endpoint; check the results on the original interface, including cancellation during validation, unavailable endpoint, missing DHCP/gateway, and verified/failed rollback. On one AX201, require serial tests. Only test concurrent waves with two distinct physical devices and two authorized endpoints. Validate Windows owns IPv4 and routing throughout.

Do not treat a missing driver response, an optional test left off, or an unavailable lab as a pass. Record `NOT_TESTED_ON_HARDWARE` for each unobserved field capability. Do not share keys, credentials, network lists or raw machine identifiers.
