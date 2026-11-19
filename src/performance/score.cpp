#include "campuslink/network.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <numeric>
#include <tuple>
#include <map>

namespace campuslink::net {
Distribution summarize(std::vector<double> samples) {
    std::erase_if(samples, [](double x) { return !std::isfinite(x) || x < 0; });
    std::sort(samples.begin(), samples.end());
    Distribution d;
    d.count = static_cast<unsigned>(samples.size());
    if (!d.count) return d;
    d.mean = std::accumulate(samples.begin(), samples.end(), 0.) / d.count;
    d.minimum = samples.front(); d.maximum = samples.back();
    d.median = (samples[(d.count - 1) / 2] + samples[d.count / 2]) / 2.;
    if (d.count >= 5) d.p95 = samples[(d.count * 95 + 99) / 100 - 1];
    return d;
}
void summarize_observation(Observation& o) {
    o.rtt_distribution = summarize({o.latency_history.begin(), o.latency_history.end()});
    o.dns_distribution = summarize(o.dns_history);
    o.latency_ms = o.rtt_distribution.mean; o.dns_ms = o.dns_distribution.median;
    if (o.attempts) o.loss_percent = 100. * (o.attempts - o.replies) / o.attempts;
    if (o.latency_history.size() > 1) {
        double sum{};
        for (std::size_t i = 1; i < o.latency_history.size(); ++i) sum += std::abs(o.latency_history[i] - o.latency_history[i - 1]);
        o.jitter_ms = sum / (o.latency_history.size() - 1);
    }
    unsigned sent{}, verified{};
    o.repeatability.reset();
    o.sample_complete = !o.transfers.empty();
    for (const auto& series : o.transfers) {
        const auto up = summarize(series.up), down = summarize(series.down);
        sent += series.attempts; verified += std::min(up.count, down.count);
        if (up.count < 2 || down.count < 2 || series.attempts != up.count || series.attempts != down.count) o.sample_complete = false;
        // The final (largest requested) transfer size is the score input, never a mix of sizes.
        o.up_mbps = up.median; o.down_mbps = down.median;
        o.repeatability.reset();
        if (down.count >= 2 && down.median && *down.median > 0)
            o.repeatability = 100. * (1. - std::clamp((*down.maximum - *down.minimum) / *down.median, 0., 1.));
    }
    o.endpoint_verified = verified > 0;
    if (sent && o.attempts) o.stability = std::min({100. * verified / sent, 100. * o.replies / o.attempts, o.repeatability.value_or(100.)});
}
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
    out.eligible = out.coverage >= .75 && o.attempts >= 3 && o.replies >= 2 && o.endpoint_verified && (o.transfers.empty() || o.sample_complete);
    return out;
}
Score candidate_score(const Candidate& candidate, std::array<double, 5> weights) {
    auto rating = score(candidate.observation, weights);
    rating.eligible = rating.eligible && candidate.host_validated;
    return rating;
}
std::vector<Candidate> rank(std::vector<Candidate> candidates, Clock::time_point now) {
    using Key = std::tuple<std::string, std::string, std::string, std::string>;
    const auto key = [](const Candidate& c) { return Key{c.bss.interface_id, c.bss.bssid, c.bss.profile, c.method}; };
    std::map<Key, std::size_t> newest;
    for (std::size_t i = 0; i < candidates.size(); ++i) {
        if (candidates[i].observed > now) continue;
        auto [it, inserted] = newest.try_emplace(key(candidates[i]), i);
        if (!inserted && candidates[it->second].observed <= candidates[i].observed) it->second = i;
    }
    for (std::size_t i = 0; i < candidates.size(); ++i) {
        auto& c = candidates[i];
        const auto it = newest.find(key(c));
        if (!c.host_validated || it == newest.end() || it->second != i || now - c.observed > std::chrono::minutes(5)) c.rating.eligible = false;
    }
    std::stable_sort(candidates.begin(), candidates.end(), [](const Candidate& a, const Candidate& b) {
        if (a.rating.eligible != b.rating.eligible) return a.rating.eligible;
        if (a.rating.points != b.rating.points) return a.rating.points > b.rating.points;
        return std::tie(a.bss.interface_id, a.bss.bssid) < std::tie(b.bss.interface_id, b.bss.bssid);
    });
    return candidates;
}
bool should_switch(const Candidate& from, const Candidate& to, Clock::time_point now, Clock::time_point last_switch) {
    using namespace std::chrono_literals;
    return from.host_validated && to.host_validated && !from.method.empty() && from.method == to.method &&
        from.bss.interface_id == to.bss.interface_id && from.bss.bssid != to.bss.bssid && from.rating.eligible && to.rating.eligible && from.observed <= now &&
        to.observed <= now && now - from.observed <= 5min && now - to.observed <= 5min && now - last_switch >= 60s &&
        to.rating.points >= from.rating.points + 10.;
}
}
