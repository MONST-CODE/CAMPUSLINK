#pragma once
#include <array>
#include <cstddef>

namespace campuslink::lab {
// Deterministic offline model; no sockets and no measured network throughput.
enum class Scheduler { best_single, round_robin, earliest_finish };
struct Path {
    unsigned rtt_ms{}, kib_per_second{}, loss_every{}, jitter_ms{};
    unsigned outage_start_ms{}, outage_end_ms{}; // half-open; equal means no outage
};
struct Scenario {
    std::array<Path, 2> paths;
    unsigned shared_kib_per_second{}; // 0: no shared capacity constraint
    unsigned segments{128}; // each is 16 KiB
};
struct Result {
    double elapsed_ms{}, mbps{};
    unsigned completed{}, retries{}, reordered{}, peak_buffer{};
    std::array<unsigned, 2> delivered{};
    bool valid{};
};
Result compare(const Scenario&, Scheduler);
}
