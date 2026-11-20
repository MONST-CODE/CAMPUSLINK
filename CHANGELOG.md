# Changelog

## 0.4.2 — Reliable connection assistant and new workspace

- Slow DHCP no longer causes a disconnection: separate association (25 s) and address (40 s) budgets; the association is kept (*address pending*) unless a usable previous link must be restored.
- Failures classified from Windows WLAN reason codes (authentication, profile, radio, address, request) with Windows' message; assisted mode tries other access points only after radio/association failures.
- Internet report polled until Windows concludes (≤ 12 s); captive portals detected (NCSI WEBHIJACK) with an *Open the sign-in page* action.
- Scans no longer come back empty on busy sites: networks heard in the last two minutes are used when a fresh scan is not confirmed.
- New *Wi-Fi connection* workspace: networks grouped by name, current connection, explained modes, recommended start access point, engine-driven progress, actionable verdicts. Design system with separate light/dark themes; lab pages grouped.
- Minimized idle CPU: 99.7 % → 0.6 % of a core. Audit report, ADR 0003, 0.4.2 hardware checklist. Unit tests 56.

## 0.4.1 — Connectivity evidence and path experiments

- Configurable bounded assistance with expiring session history, attempt explanations and separate association/IP/OS Internet evidence.
- Read-only decoding of the documented Windows Dual-STA interface list and synchronization state.
- Per-interface scoped endpoint measurement without reconnecting, plus a separate offline scheduler lab for capacity, shared bottleneck and outage scenarios.
- Avoid redundant idle UI snapshot copies; preserve software/Vulkan rendering and correct the portable quick start.

## 0.4.0 — Wi-Fi connection assistant

- Home starts with Wi-Fi discovery and gives a choice of two independent connection modes without a lab scope.
- Normal mode uses the Windows profile and leaves BSSID selection to Windows. Direct assisted mode tries the selected BSSID and at most one compatible alternative, preferring an idle independent physical adapter when available.
- Verify the observed association and usable IPv4 gateway, display per-adapter Windows Internet evidence separately, and preserve local-only links for captive portal sign-in.
- Confirm disruptive actions, bound attempts, and report verified recovery or recovery failure. The authorized CLP1 lab and offline simulator remain separate.

## 0.3.0 — Bilingual desktop workspace

- French-first runtime localization, with an instant FR/EN switch and a checked catalog.
- A sky-blue visual identity, separately designed light/dark themes, responsive navigation and a confirmation dialog for physical trials.
- Inter and JetBrains Mono with persisted font size, layout density and window size.
- A bilingual About and quick-start page with actual build metadata.
- Windows portable ZIP assets and notices are hashed, and ZIP paths must use forward slashes.

## 0.2.1 — Validation and lab operability

- Assisted handoff requires a host-validated result on the same interface. The latest failed measurement supersedes older successful measurements for that interface, BSSID and method.
- Browse all completed logical clients in pages, inspect individual histories and replay a new in-place DHCP renewal scenario without restarting discovery.
- Expose current PHY, the 802.11 interface type, 802.11d, frame-protection and legacy Hosted Network reports without treating them as simultaneous STA evidence.
- Include network diagnostics in the portable guide and enforce the required operator-kit files during verification.

## 0.2.0 — Product completion candidate

- Native radio evidence now distinguishes vendor documentation, driver reports, active OS associations and validated trial windows. Secondary STA returns a native result, not an inferred ceiling.
- Progressive capacity profiling samples process RSS and names the stop cause (`LIMIT_REACHED`, budget or cancellation). Lab models offer ownership, ACK lease start and DHCP retry backoff; logical histories can be inspected and cleared.
- Explicit authorized batches serialize candidates unless distinct physical PnP devices and endpoints permit parallel waves. Handoff verifies stable Windows IPv4 and confirms rollback state.
- CLP1 tests multiple sizes and paced repetitions, with stabilization, RTT distribution, DNS timing, per-size statistics and sample sufficiency. The score and assisted selection compare matching methods only.
- Eleven-panel render smoke, opt-in native Vulkan smoke, portable Windows 11 acceptance tooling, CI packaging verification and versioned draft-release preparation.

## 0.1.0 — Candidate

- SDL3 + Dear ImGui desktop with Vulkan-first SDL GPU and software fallback.
- Offline logical clients, DHCP simulator, scenario replay/CSV, deterministic scenarios and a measured CPU/wall-time capacity profiler.
- Windows-native inventory, WLAN scan and operator-authorized handoff with post-switch IPv4 and controlled endpoint checks and optional rollback.
- Bounded ICMP and CLP1 metrics with confidence-aware ranking.
- Direct Debug/Release compilation, unit/smoke tests and verified portable ZIP/local release commands. See the delivery report for the execution status of each gate.
