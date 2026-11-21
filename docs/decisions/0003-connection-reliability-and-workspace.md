# ADR 0003 — Connection reliability and the connection workspace (0.4.2)

**Status:** adopted for 0.4.2. Baseline: `2940f2a` (0.4.1 + audit docs). Audit: [AUDIT_0.4.0_0.4.1](../reports/AUDIT_0.4.0_0.4.1.md).

## Context

The comparative audit found that both 0.4.0 and 0.4.1 gave Windows a single 15 s budget for association, DHCP and gateway stability, then called `WlanDisconnect`; read the Windows Internet report once, before NCSI concludes; discarded the whole scan when the completion notification took more than 5 s; and tried other BSSIDs after authentication or address failures. 0.4.1 made slow networks worse (three attempts by default, a mid-run scan expiry, *unsettled* when Windows auto-reconnected during cleanup). Both versions spun a CPU core while minimized. The connection flow lived inside a 13-panel research workbench.

## Decisions

1. **Phase budgets, never tear down an association for a slow address.** Native `join()` waits for the association outcome (ACM `connection_complete`/`connection_attempt_fail` notifications, or the observed state) for at most 25 s, then for a stable usable IPv4 address and gateway for at most 40 s. Only a request that is still pending in phase A is cancelled at adapter level. Windows establishing any association itself during cleanup is a settled state.
2. **Classify failures from Windows' reason code** (`classify_wlan_reason`, documented L2 reason groups): authentication (802.11 security and 802.1X), saved profile, radio/association, address, request refused. `WlanReasonCodeToString` provides Windows' own message.
3. **Retry policy.** Alternatives of the same network are tried only after radio or unconfirmed-association failures (`retry_elsewhere`). Authentication, profile, request and address failures are network-wide: stop and explain. Total attempts stay bounded (3 by default, 4 maximum).
4. **Never leave the user worse off.** On an address timeout, keep the new association (`ip_pending`) unless a previously usable link was displaced; then restore and verify it (one restoration per operation, as before).
5. **Settle the Internet report.** Poll Network List Manager every 0.75 s for up to 12 s while it reports local-only; `unknown` is not waited for. Check `NA_InternetConnectivityV4/V6` WEBHIJACK first and report `portal`.
6. **Scan freshness without empty results.** Wait up to 8 s per adapter (15 s total) for `scan_complete`; otherwise use Windows' list, dropping entries whose host timestamp is older than 120 s, and flag `Discovery::cached`. The 2-minute freshness rule now gates the start of an operation only.
7. **Explainable start pre-selection** (`recommend_start`): reported RSSI, plus 6 dB for 5/6 GHz at or above −72 dBm. The same rule orders alternatives after history and adapter independence. It is displayed as a preference, never as throughput.
8. **Engine-confirmed progress.** `WifiPolicy::progress` publishes `preparing → associating → obtaining_address → checking_internet` (and `restoring`). The UI draws only these stages.
9. **Connection workspace and design system.** Separate light/dark tokens with WCAG-checked contrast, spacing and type scale, small stateless widgets. Networks grouped by SSID; current connection; two explained modes; verdicts with causes and actions. Lab pages grouped under *Research lab*.
10. **Event-driven rendering.** Render while the worker runs or for three frames after input; otherwise wait for events with a 250 ms timeout; do not render while minimized, hidden or occluded.

## State machine

```text
            ┌──────────── cancel (any state) ───────────────────────────────┐
            ▼                                                               │
  idle ─scan─▶ discovered ─select─▶ ready ─connect─▶ preparing ─plan─▶ associating
                                                        │ invalid           │ │ │
                                                        ▼                   │ │ └─ auth/profile/request fail ─▶ failed
                                                     invalid                │ └── radio/association fail ─▶ [restore?] ─▶ next AP or failed
                                                                            ▼
                                                                 obtaining_address ── 40 s, nothing displaced ─▶ ip_pending (association kept)
                                                                            │      └─ 40 s, usable link displaced ─▶ restoring ─▶ failed / recovery_failed
                                                                            ▼
                                                                 checking_internet ─▶ internet_reported | portal | local_link
```

Every transition is decided in `connect_wifi` from backend observations; the UI receives the state, never infers one.

## Multipath and aggregation trajectory

0.4.2 keeps ADR 0002's boundary: no relay, gateway, tunnel or driver in the product. The intended separation, when hardware and an authenticated endpoint exist:

| Component | Responsibility | Status |
|---|---|---|
| Host | Consumes paths; owns policy and UI | Connection assistant shipped |
| Path manager | Enumerates verified paths (interface + source + authorized endpoint) and measures each without reconnecting | `observe_path` (CLP1) shipped, lab only |
| Multipath scheduler | Splits work across verified paths with bounded reordering | Offline models only (best single / round robin / earliest finish) |
| Node | A volunteer device keeping its own authorized access and offering a transport | Not implemented; requires pairing, mutual authentication, revocation, quotas |
| Gateway | Self-hostable endpoint that reassembles application-level transfers | Not implemented; first prototype planned as HTTPS Range over two verified paths (ADR 0002) |
| Secure transport | TLS from the platform; no custom cryptography | Not implemented |

Gates before any of these ship: two physically independent paths verified on owned equipment, a shared-bottleneck test, a fairness check against single-path traffic, and explicit opt-in. A node never passes its portal authorization or lease to the host.

## Consequences

- Slow networks keep a progressing association; failures carry an actionable cause; assisted mode no longer hops after authentication errors.
- The UI is simpler for the main path and still exposes every lab tool.
- New unit tests cover the policies; hardware behaviour remains to be accepted on Windows 11 (see WINDOWS11_VALIDATION 0.4.2 additions).
- `oleaut32` is linked for `VariantClear` (property bag).

References: [WLAN_CONNECTION_NOTIFICATION_DATA](https://learn.microsoft.com/en-us/windows/win32/api/wlanapi/ns-wlanapi-wlan_connection_notification_data), [WLAN_REASON_CODE](https://learn.microsoft.com/en-us/windows/win32/nativewifi/wlan-reason-code), [WlanReasonCodeToString](https://learn.microsoft.com/en-us/windows/win32/api/wlanapi/nf-wlanapi-wlanreasoncodetostring), [WLAN_BSS_ENTRY](https://learn.microsoft.com/en-us/windows/win32/api/wlanapi/ns-wlanapi-wlan_bss_entry), [NLM_INTERNET_CONNECTIVITY](https://learn.microsoft.com/en-us/windows/win32/api/netlistmgr/ne-netlistmgr-nlm_internet_connectivity), [WCAG 2.2 contrast](https://www.w3.org/TR/WCAG22/#contrast-minimum).
