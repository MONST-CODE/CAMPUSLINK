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

## Wi-Fi connection assistant acceptance (0.4.0)

Run on an interactive Windows 11 x64 machine using only networks you are allowed to join. Use a saved profile, two BSSIDs for one network if available, and a browser to independently confirm actual portal/Internet behavior. Do not perform the interruption tests through a remote session that depends on the adapter under test.

| Scenario | Expected evidence |
|---|---|
| First launch, no scope file | No simulated clients or automatic connection. Home action starts discovery. |
| Select a saved network and use normal mode | Windows may choose a different BSSID. UI shows the observed association, IPv4, gateway and adapter. |
| Restart, select the network, choose direct assistance first | No prior normal attempt required. First request pins the selected BSSID. |
| First AP cannot associate/configure IP | At most one compatible alternative. On a single adapter these are sequential. An actively connected additional adapter is not used. |
| Two separate physical adapters, second idle | If an eligible alternative is present and selected AP fails, confirm the second adapter is the actual fallback. |
| Captive or restricted network | Local link without confirmed Internet is retained. User can complete sign-in in Windows; confirm no automatic probe traffic from CAMPUSLINK. |
| Cancel a link change or make it fail | No further attempts after cancellation or failed recovery; compare the previous profile and BSSID with the actual adapter state. |
| No profile, location denied, radio disabled | No new profile/credential is invented; Windows settings or a native error is available. |

Record model, driver, Windows build and the observed per-adapter result, omitting credentials. Until run, the rows above are `NOT_TESTED_ON_HARDWARE`.

## 0.4.1 additions

- Run the normal and direct modes independently as a standard user; cover a fresh attempt, missing profile, stale scan, denied location, absent IP, and explicit cancellation. Compare the recorded association/local-link/NLM times with native state observations, not RF timestamps. Check that a competing external association is not intentionally overwritten during recovery.
- On a driver exposing Dual-STA, compare each returned interface GUID/state and the synchronization flag with the Microsoft Native Wi-Fi sample. Verify opening the inventory does not change the flag. If the driver is absent, record `NOT_TESTED_ON_HARDWARE`, not a zero-capacity verdict.
- Load an authorized CLP1 scope for a current association. Measure it from Paths without reconnecting. Verify actual source and egress on owned equipment, then repeat on a separately identified second adapter. Verify that changing the BSSID or address during measurement does not credit the sample. This is an endpoint test, not an aggregate throughput test.
- Use the offline schedulers without a network connection. Confirm that the lab marks its goodput, reordering and retry figures as simulated and that a physical path is not marked active based on the lab.

## 0.4.2 additions

Run as a standard user on the Dell/AX201 (or another named adapter). Record Windows build, driver version and, for each row, the result shown by CAMPUSLINK next to `netsh wlan show interfaces` and `ipconfig`. Never record passwords. Rows not run stay `NOT_TESTED_ON_HARDWARE`. Executed so far (2026-11-20): rows 1 (campus, recent-list fallback), 2 (normal and assisted) and a variant of 7 (out-of-range saved network, previous link restored) — see [FIELD_0.4.2](reports/FIELD_0.4.2.md).

| # | Scenario | Expected behaviour |
|---|---|---|
| 1 | Dense site, press **Search for networks** | Networks grouped by name; if Windows does not confirm the scan within 8 s, the *recent networks* banner appears and the list is not empty when `netsh wlan show networks` lists networks. |
| 2 | Normal connection to a saved network that is in range | Steps Preparation → Association → IP address → Internet; *Connected* with association/address/Internet timings in **Details**. |
| 3 | Slow DHCP (busy network, or DHCP server delayed on owned equipment) | No disconnection by CAMPUSLINK; *Associated, address pending* if no address after 40 s; Windows later completes or reports the issue. With a previously usable link on the same adapter, that link is restored instead. |
| 4 | Wrong saved password (owned network) | *Connection failed* with cause **Authentication** and the Windows message; assisted mode does **not** try other access points. |
| 5 | Selected AP switched off or out of range, other AP of the same network available (assisted) | Cause **Radio / association**; the next access point is tried; result on the alternative. |
| 6 | Captive portal | *Sign-in required* (or *Internet not confirmed* if Windows does not flag WEBHIJACK); **Open the sign-in page** opens the Windows probe URL; the association is kept. |
| 7 | Switch from network A to B, B fails | Confirmation dialog before the switch; after failure, A is restored and *Your previous connection was restored* is shown. |
| 8 | **Cancel** during association / during address | No further attempt; previous link restored when it was displaced. |
| 9 | Minimize for 60 s, then restore | Process CPU near zero while minimized (Task Manager); window redraws immediately on restore. |
| 10 | Assisted recommendation | With a 2.4 GHz and a 5 GHz AP of the same saved network, the *Recommended* access point follows the documented rule and its reason is displayed. |

Developer evidence for UI layout comes from `CampusLink.exe --ui-smoke` with `CAMPUSLINK_CAPTURE_DIR` (sample-data badge on fixture states). `CAMPUSLINK_CAPTURE_LIVE=1` adds one page rendered from a real read-only inventory and scan; those images show nearby networks and must not be published.
