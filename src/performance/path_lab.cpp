#include "campuslink/path_lab.hpp"
#include <algorithm>
#include <limits>
#include <vector>

namespace campuslink::lab {
namespace {
Result run(const Scenario& model, Scheduler strategy, unsigned baseline) {
    constexpr unsigned segment_kib = 16, window = 16;
    constexpr double unavailable = std::numeric_limits<double>::infinity();
    Result out;
    if (!model.segments || model.segments > 4096 || model.shared_kib_per_second > 1'000'000) return out;
    for (const auto& p : model.paths)
        if (!p.kib_per_second || p.kib_per_second > 1'000'000 || p.rtt_ms > 10'000 || p.jitter_ms > 1'000 ||
            p.outage_end_ms < p.outage_start_ms || p.outage_end_ms > 3'600'000 || p.loss_every == 1) return out;
    out.valid = true;
    const auto service = [&](unsigned i) { return 1000. * segment_kib / model.paths[i].kib_per_second; };
    std::array<double, 2> next_send{};
    std::array<unsigned, 2> sends{};
    double shared{}, now{};
    unsigned expected{}, buffered{};
    struct Event { double at{}; unsigned segment{}, path{}, retries{}; bool lost{}; };
    std::vector<Event> pending;
    pending.reserve(window);
    std::vector<bool> arrived(model.segments);
    const auto send = [&](unsigned seq, unsigned retries, int avoid) {
        const auto start = [&](unsigned i) {
            const auto& p = model.paths[i];
            auto t = std::max(now, next_send[i]);
            if (model.shared_kib_per_second) t = std::max(t, shared);
            if (t >= p.outage_start_ms && t < p.outage_end_ms) t = p.outage_end_ms;
            return t;
        };
        const auto eta = [&](unsigned i) { return start(i) + service(i) + model.paths[i].rtt_ms; };
        unsigned i = baseline;
        if (strategy == Scheduler::round_robin) i = seq % 2;
        if (strategy == Scheduler::earliest_finish) i = eta(0) <= eta(1) ? 0u : 1u;
        if (avoid >= 0 && strategy != Scheduler::best_single && i == static_cast<unsigned>(avoid)) i ^= 1u;
        const auto& p = model.paths[i];
        const auto begin = start(i);
        const auto end = begin + service(i);
        next_send[i] = end;
        if (model.shared_kib_per_second) shared = std::max(now, shared) + 1000. * segment_kib / model.shared_kib_per_second;
        const auto ordinal = ++sends[i];
        const double transit = p.rtt_ms + static_cast<double>((seq * 31u + ordinal * 17u) % (p.jitter_ms + 1));
        const bool lost = (p.loss_every && ordinal % p.loss_every == 0) ||
            (p.outage_start_ms < p.outage_end_ms && begin < p.outage_start_ms && end + transit >= p.outage_start_ms);
        pending.push_back({end + transit + (lost ? std::max(50u, p.rtt_ms) : 0u), seq, i, retries, lost});
    };
    unsigned next{};
    while (expected < model.segments) {
        while (next < model.segments && next - expected < window) send(next++, 0, -1);
        const auto earliest = std::min_element(pending.begin(), pending.end(), [](const Event& a, const Event& b) {
            return a.at == b.at ? a.segment < b.segment : a.at < b.at;
        });
        if (earliest == pending.end() || earliest->at >= unavailable) break;
        const auto event = *earliest;
        pending.erase(earliest);
        now = event.at;
        if (event.lost) {
            if (event.retries == 4) break;
            ++out.retries;
            send(event.segment, event.retries + 1, static_cast<int>(event.path));
            continue;
        }
        ++out.completed;
        ++out.delivered[event.path];
        arrived[event.segment] = true;
        ++buffered;
        if (event.segment != expected) ++out.reordered;
        while (expected < model.segments && arrived[expected]) { ++expected; --buffered; }
        out.peak_buffer = std::max(out.peak_buffer, buffered);
    }
    out.elapsed_ms = now;
    if (expected == model.segments && now > 0) out.mbps = double(model.segments) * segment_kib * 1024 * 8 / (1000 * now);
    return out;
}
}
Result compare(const Scenario& model, Scheduler strategy) {
    if (strategy != Scheduler::best_single) return run(model, strategy, 0);
    const auto a = run(model, strategy, 0), b = run(model, strategy, 1);
    if (a.completed != b.completed) return a.completed > b.completed ? a : b;
    return a.elapsed_ms <= b.elapsed_ms ? a : b;
}
}
