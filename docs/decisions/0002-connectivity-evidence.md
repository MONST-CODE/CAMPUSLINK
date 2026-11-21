# ADR 0002 — Evidence before transport (0.4.1)

## Baseline and audit

Reference `018cb95f2d9b56091d5586bf4764b8022c53a119`; package tag `7ad1e93d943ed423b2474e3a591ed409643ade7d`. The intervening commits add validation evidence, not executable changes. GitHub reports the same portable ZIP digest as the 0.4.0 CI. The binary download redirects outside the sandbox allowlist; the stale packaged README is independently explained by `scripts/package.ps1`, which maps `docs/PORTABLE_README.md` at the tag to `README.md` without substitution. Existing screenshots show software rendering, not WLAN interaction.

```text
SDL/ImGui → worker → connection policy → Backend → WLAN / IP Helper / NLM
                  → scoped experiments → Backend → bound CLP1 / ICMP / DNS
                  → offline DHCP and path-policy experiments (no sockets)
```

0.4.0 already separates these flows; preserve that boundary. Problems: fallback budget fixed at two, no per-attempt lifecycle evidence, no adaptive ranking outside scoped CLP1, Dual-STA payload only counted in bytes, per-frame use of periodically copied worker snapshots. Native CLP1 requests `IP_UNICAST_IF` and binds the IPv4 source, and IP Helper supplies route evidence; these are configuration/OS evidence, not a capture on the physical wire. CLP1 is plaintext synthetic traffic to an explicitly authorized endpoint, not a secure transport or an Internet test. A Wi-Fi association is not a path; a path requires a source/interface and a verified endpoint, and two paths may share bottlenecks.

## Decisions

1. Preserve normal profile-only connect and explicit assisted BSSID selection. Rank alternatives by fresh successful/failed association evidence, then observed RSSI, gated by exact SSID/profile/security evidence and independent idle physical adapters. Limit to 1–4 candidates, default 3; no switches after success, cancellation, lost freshness or unresolved recovery. Session-local history is bounded and not exported; only actual targeted observations influence the policy. Preserve local links for Windows sign-in. No new credentials, no app-supplied profile XML. Re-evaluate open/secure first-join when hardware validation exists; no silent profile persistence.
2. Decode the documented secondary `WLAN_INTERFACE_INFO_LIST` with length checks; query synchronization status separately. Microsoft documents synchronization with the primary and a request scoped to a live WLAN handle. Capability inspection must not enable it or infer independent radios. Actual activation is deferred pending a driver and controlled field test.
3. Keep a no-socket scheduler lab separate from both paths and DHCP simulation. Measure relative scheduler behaviour under modeled latency, service rate, common bottleneck and path loss. Ship no relay, arbitrary traffic forwarding or privileged driver until authenticated endpoints and controlled physical paths can be tested. No heavy new dependency.

## Transport comparison

| Option | Feasibility / privilege | Correctness and validation gate |
|---|---|---|
| Native MPTCP | Linux exposes `IPPROTO_MPTCP`; no Windows 11 API established for this build. Endpoint negotiation and actual subflows needed. | Verify fallback, subflow source, NAT/middlebox behaviour, coupled congestion and failure. Do not treat two Winsock connections as an MPTCP session. |
| Secure gateway, outbound sessions | Independent user-mode TLS/QUIC sessions can traverse approved NAT boundaries without LAN reachability. QUIC v1 migration is not concurrent aggregation. Transparent system capture is separate, privileged. | Opt-in mutual authentication with revocation, session/traffic/time quotas; no executable control messages. Measure reorder, backpressure and fairness, loss/fallback; avoid a general TCP tunnel inside TCP. Gateway operators pay hosting and egress, and enforce egress/network policy. |
| Application-layer HTTPS Range | Best unprivileged follow-up prototype for a controlled immutable object. Requires strong validator, exact 206/Content-Range/length, identity transfer, bound connection and integrity verification. | Reassemble within a bounded window, retry a segment after loss, reject mixed generations; fall back on unsupported ranges. TLS uses platform certificates; this is not a VPN. Compare against fastest single path and a shared-bottleneck test. |
| Direct authorized LAN/USB relay | Can avoid remote infrastructure on private links; public isolation/NAT defeats LAN discovery; Ethernet/USB hardware may help. | Pair explicitly, no port scan or unsolicited use, verify endpoint and each node’s own access/policy. Do not share credentials or pass portal authorization across nodes. |
| Local proxy / WFP / TUN | Explicit proxy only supported apps. WFP and Wintun-style adapters require separate analysis of distribution, signed install, privileges and uninstall. | No driver installation, route replacement, DNS changes or interception in the portable base. |

Primary references: [Native Wi-Fi connection parameters](https://learn.microsoft.com/en-us/windows/win32/api/wlanapi/ns-wlanapi-wlan_connection_parameters), [Permissions](https://learn.microsoft.com/en-us/windows/win32/nativewifi/native-wifi-api-permissions), [Location changes](https://learn.microsoft.com/en-us/windows/win32/nativewifi/wi-fi-access-location-changes), [Dual-STA documented ABI and lifetime](https://learn.microsoft.com/en-us/windows/win32/nativewifi/dual-sta-query-config), [Winsock IP options](https://learn.microsoft.com/en-us/windows/win32/winsock/ipproto-ip-socket-options), [Linux MPTCP](https://www.kernel.org/doc/html/latest/networking/mptcp.html), [RFC 8684](https://www.rfc-editor.org/rfc/rfc8684), [RFC 6182](https://www.rfc-editor.org/rfc/rfc6182), [RFC 6356](https://www.rfc-editor.org/rfc/rfc6356), [RFC 9000](https://www.rfc-editor.org/rfc/rfc9000), [RFC 9110 §14](https://www.rfc-editor.org/rfc/rfc9110.html#section-14), [RFC 8910](https://www.rfc-editor.org/rfc/rfc8910), [Wintun](https://www.wintun.net/).
