#include "campuslink/network.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <tuple>

namespace campuslink::net {
Score score(const Observation& o, std::array<double, 5> weights) {
    Score out{};
    const std::array metrics{o.stability, o.down_mbps, o.latency_ms, o.loss_percent, o.jitter_ms};
    constexpr std::array target{100., 100., 200., 100., 80.};
    double total{}, active{}, sum{};
    for (std::size_t i = 0; i < weights.size(); ++i) {
        const auto w = weights[i];
        if (!std::isfinite(w) || w < 0 || w > 100) throw std::invalid_argument("Invalid quality weight");
        total += w;
        if (!metrics[i] || !std::isfinite(*metrics[i]) || *metrics[i] < 0) continue;
        auto component = std::clamp(*metrics[i] / target[i], 0., 1.) * 100.;
        if (i >= 2) component = 100. - component;
        out.components[i] = component;
        if (w > 0) { active += w; sum += w * component; }
    }
    if (total <= 0) throw std::invalid_argument("All quality weights are zero");
    out.coverage = active / total;
    out.points = active > 0 ? sum / active : 0.;
    out.samples = o.attempts;
    out.eligible = out.coverage >= .75 && o.attempts >= 3 && o.replies >= 2 && o.endpoint_verified;
    return out;
}
std::vector<Candidate> rank(std::vector<Candidate> candidates, Clock::time_point now) {
    for (auto& c : candidates) if (c.observed > now || now - c.observed > std::chrono::minutes(5)) c.rating.eligible = false;
    std::stable_sort(candidates.begin(), candidates.end(), [](const Candidate& a, const Candidate& b) {
        if (a.rating.eligible != b.rating.eligible) return a.rating.eligible;
        if (a.rating.points != b.rating.points) return a.rating.points > b.rating.points;
        return std::tie(a.bss.interface_id, a.bss.bssid) < std::tie(b.bss.interface_id, b.bss.bssid);
    });
    return candidates;
}
bool should_switch(const Candidate& from, const Candidate& to, Clock::time_point now, Clock::time_point last_switch) {
    using namespace std::chrono_literals;
    return from.bss.bssid != to.bss.bssid && from.rating.eligible && to.rating.eligible && from.observed <= now &&
        to.observed <= now && now - from.observed <= 5min && now - to.observed <= 5min && now - last_switch >= 60s &&
        to.rating.points >= from.rating.points + 10.;
}
}
