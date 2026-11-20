# CAMPUSLINK 0.4.2 — Performance measurements

Machine: Windows 11 Pro 26200, Intel Wi-Fi 6 AX201, Vulkan available, campus Wi-Fi in use (gateway RTT 0.4–1.2 s). Date: 2026-11-20. Values are single-machine observations, not guarantees.

## Idle CPU while minimized

Launched minimized without focus, 5 s settle, CPU time sampled over 10 s, process killed afterwards (no interaction).

| Binary | CPU (% of one core) | Working set |
|---|---:|---:|
| 0.4.0 official package | 99.5 | 104.8 MB |
| 0.4.1 official package | 100.0 / 99.7 (two runs) | 83.2 MB |
| 0.4.1 source, MinGW dev build | 99.8 | 84.4 MB |
| 0.4.2 MinGW dev build | **0.6** | 81.9 MB |

Cause in 0.4.x: with the Vulkan (SDL GPU) path, a minimized window returns no swapchain texture and the loop never waits. 0.4.2 waits for events when idle and does not render while minimized, hidden or occluded. The visible-window idle rate on a GPU was not measured (it would require driving the user's desktop).

## UI build cost (CPU, `screen.draw` only)

Identical instrumentation added to temporary copies of 0.4.1 and 0.4.2 (not committed), same MinGW clang 22 `-O2` build, `--ui-smoke` page frames only (first frame of each of 13 pages × 4 language/theme/size/DPI sets = 52 samples), 5 paired runs.

| Run | 0.4.1 p50 / p95 (ms) | 0.4.2 p50 / p95 (ms) |
|---|---|---|
| 1 | 0.096 / 0.330 | 0.127 / 0.625 |
| 2 | 0.094 / 0.384 | 0.132 / 0.561 |
| 3 | 0.095 / 0.341 | 0.130 / 0.522 |
| 4 | 0.096 / 0.390 | 0.128 / 0.489 |
| 5 | 0.099 / 0.294 | 0.132 / 0.508 |

+0.03 ms per frame at the median: negligible against a 16.7 ms frame. Pages differ in content (0.4.2 connection workspace vs 0.4.1 overview), so this compares the UI as shipped, not identical widgets.

## Offscreen software frame time

`UI_SMOKE_FRAME_MS` includes SDL software rasterization at injected DPI up to 2.0. 0.4.1: p50 ≈ 54 ms over 52 frames; 0.4.2: p50 ≈ 68–82 ms over 240 frames (pages plus seven connection states, three frames each). The sets differ; the increase comes from pixel fill (cards, rounded corners), not logic (see above). It affects only the software fallback while the UI is changing; Vulkan rendering is unaffected.

## Startup and memory (official 0.4.x, for reference)

Window visible after 269–336 ms; working set ≈ 161 MB at 3.5 s for both 0.4.0 and 0.4.1 (visible window, Vulkan). 0.4.2 official-package startup is to be measured on the CI-built package.

## Network timing

Association, address and Internet-report times are recorded per attempt in **Details**. No connection was performed on this machine for 0.4.2 (it would interrupt the user's session): `NOT_TESTED_ON_HARDWARE`.
