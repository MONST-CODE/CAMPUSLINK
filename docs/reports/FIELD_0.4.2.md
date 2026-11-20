# CAMPUSLINK 0.4.2 — Field results on the reference PC

Date: 2026-11-20. Windows 11 Pro 26200, Intel Wi-Fi 6 AX201 160 MHz, standard user session, user-authorized tests. Driver: the engine (`connect_wifi`) and Windows backend objects of the 0.4.2 developer build, linked into a non-shipped console harness (no UI automation). Network identifiers are redacted.

## Environment observations

| Observation | Result |
|---|---|
| Campus site (dense, gateway RTT 0.4–1.2 s) | `WlanScan` notification not received within 8 s on repeated scans; 0.4.0/0.4.1 showed **no networks**. 0.4.2 used Windows' recent list (5 networks, banner shown). |
| Phone hotspot site (2 networks in range) | Fresh scan confirmed in 0.5–3.7 s (`cached=0`). |
| NLM report on an associated adapter | `internet` read without error; property-bag query for portal did not fail. |

## Connection tests (phone hotspot, 5 GHz, saved profile)

| # | Scenario | Result | Association | Usable IPv4 | Internet (Windows) |
|---|---|---|---:|---:|---:|
| 1 | Adapter disconnected, **normal** connection | `internet_reported`, 1 attempt | 328 ms | 818 ms | 872 ms |
| 2 | Adapter disconnected, **assisted** (BSSID pinned) | `internet_reported`, 1 attempt | 350 ms | 845 ms | 897 ms |
| 3 | Connected to the hotspot; **normal** request for a saved campus profile out of range | `failed`, cause **radio**, reason `0x2800B` (network not available), Windows message shown; previous link **restored and verified** | — | — | — |

Test 3 detail: failure known 2.1 s after the request (from the ACM notification; 0.4.x waited for a 15 s timeout and then disconnected); restoration completed 0.9 s later; total 3.1 s. After the run, `netsh wlan show interfaces` showed the hotspot connected.

Note: Windows may also auto-connect the profile after `netsh wlan disconnect`; the timings in tests 1–2 therefore measure the first observation of each state, not radio-level timestamps.

## Not run here (`NOT_TESTED_ON_HARDWARE`)

Slow DHCP (address pending), wrong password (authentication class), captive portal flag, a second access point of the same network (radio failover), a second physical adapter, visible-window GPU frame pacing. See [WINDOWS11_VALIDATION.md](../WINDOWS11_VALIDATION.md#042-additions).
