# Known limitations

## Validation scope

- Windows Server CI and offscreen SDL rendering cannot replace Windows 11 client, WLAN radio, Dell/AX201, visible display or Vulkan device-loss acceptance. The 0.4.2 hardware checks still marked `NOT_TESTED_ON_HARDWARE` are listed in [WINDOWS11_VALIDATION.md](WINDOWS11_VALIDATION.md#042-additions).
- The local developer build (`scripts/dev-mingw.ps1`, MinGW clang, SDL 3.4) is not the release toolchain. Release packages come from `build.bat` in CI (clang targeting MSVC, pinned SDL 3.2.30).

## Connection assistant

- Windows owns association, credentials, IP configuration and routes. CAMPUSLINK requests a connection with an existing profile; it does not create profiles, store passwords or sign in to captive portals.
- Failure classes come from documented WLAN reason-code ranges reported by Windows. A driver may report a generic code; then the cause is shown as *Not reported* or *Association not confirmed*.
- "Internet" is the Network List Manager (NCSI) report for the adapter, polled for up to 12 s after the address is usable. Captive portal detection relies on Windows' `NA_InternetConnectivityV4/V6` WEBHIJACK flag (Windows 10 1709+); a portal Windows does not flag appears as *Internet not confirmed*. CAMPUSLINK runs no active Internet test of its own.
- Budgets: association 25 s, address 40 s, Internet report 12 s. On very slow DHCP the association is kept (*address pending*) unless a previously usable link was displaced, in which case that link is restored.
- The assisted start pre-selection uses reported RSSI and band (5/6 GHz preferred by 6 dB above −72 dBm). It is a heuristic, not a throughput or congestion measurement.
- When Windows does not confirm a fresh scan within 8 s, entries heard within the last two minutes are used (BSS host timestamps). A driver that does not fill the timestamp keeps its cached entries.
- IPv4 only; IPv6-only networks are not validated. Multiple Wi-Fi adapters are supported for alternatives only when Windows reports distinct, idle physical devices.

## Connection-first (0.5)

- Per-network memory keys on SSID + security, not location: two different networks that share a name and security are merged. It is stored locally, holds no credentials, and is user-clearable. A remembered best access point is used as the start only when still in range above −80 dBm and with more successes than failures.
- Channel density is an access-point count on the channel (a crude congestion proxy), used only as a deterministic tiebreak between equal signals — not airtime or measured load.
- The multi-access-point sweep is bounded (`wifi_sweep_cap`); it trades a longer operation for more chances and is opt-in per connection.
- The link watch samples every ~4 s on a separate thread and proposes a roam only when the current link is at or below −70 dBm and a same-network access point improves RSSI by at least 12 dB, and only when a scan is still fresh (< 2 min). It never scans or connects on its own; "Stay" suppresses proposals for 5 minutes. RSSI is a reported value, not a throughput or stability guarantee. The watch is session-only (not persisted) and off by default.

## Research lab

- Physical CLP1 trials are IPv4-only and manually authorized per exact BSSID/profile; up to four candidates can be queued; shared devices are serialized. CLP1 rate includes protocol RTT; no wide-area or long-duration stability claim.
- Dual-STA is read-only. No relay, gateway, multipath transport or system-wide aggregation is deployed; scheduler comparisons are offline models (see ADR 0002 and ADR 0003).
- WLAN role concurrency, several channel-width details and Wi-Fi Direct/secondary STA combinations are unknown. The PnP/IP Helper inventory does not list disabled devices without an interface.
- The capacity profiler samples process RSS; transient peaks can be missed. The configured client limit is not an observed maximum.

## Rendering and performance

- Measured: minimized idle CPU and offscreen UI build cost (see [PERF_0.4.2](reports/PERF_0.4.2.md)). Visible-window frame pacing on a GPU is not measured.
- The SDL software renderer fallback fills more pixels in 0.4.2 (cards, rounded corners): offscreen software frames are slower than 0.4.1 while UI logic cost is unchanged within 0.03 ms. Vulkan is unaffected.
- No bitwise build reproducibility or separate PDB ZIP is asserted.
