#include "campuslink/simulation.hpp"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <sstream>
#include <stdexcept>
#include <tuple>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <ctime>
#endif

namespace campuslink::sim {
namespace {
using namespace std::literals;
constexpr std::array scenarios{"Healthy"sv,"DHCP pool exhausted"sv,"No DHCP response"sv,"Lease expiry and renewal"sv,
    "Invalid IP"sv,"Gateway unavailable"sv,"Slow DNS"sv,"DNS unavailable"sv,"Weak signal"sv,"Congestion"sv,
    "Loss and jitter"sv,"Unstable access"sv,"Logical IP conflict"sv,"No access point"sv,"Association rejected"sv,"Authentication denied"sv};
constexpr std::array states{"CREATED"sv,"DISCOVERY"sv,"ASSOCIATION"sv,"AUTHENTICATION"sv,"DHCP_DISCOVER"sv,"DHCP_OFFER"sv,"DHCP_REQUEST"sv,"DHCP_ACK"sv,"CONFIGURED"sv,
    "GATEWAY"sv,"DNS"sv,"ONLINE"sv,"INTERRUPTED"sv,"NO_AP"sv,"ASSOCIATION_REJECTED"sv,"AUTH_DENIED"sv,
    "DHCP_TIMEOUT"sv,"DHCP_EXHAUSTED"sv,"LOGICAL_CONFLICT"sv,"INVALID_IP"sv,"NO_GATEWAY"sv,"NO_DNS"sv,"CANCELLED"sv};
Config checked(Config c) {
    if (!c.valid()) throw std::invalid_argument("Invalid simulation configuration");
    return c;
}
double cpu_ms() {
#ifdef _WIN32
    FILETIME create{}, exit{}, kernel{}, user{};
    if (!GetProcessTimes(GetCurrentProcess(), &create, &exit, &kernel, &user)) throw std::runtime_error("GetProcessTimes failed");
    const auto ticks = [](FILETIME t) { return (static_cast<std::uint64_t>(t.dwHighDateTime) << 32U) | t.dwLowDateTime; };
    return static_cast<double>(ticks(kernel) + ticks(user)) / 10000.0;
#else
    return static_cast<double>(std::clock()) * 1000.0 / CLOCKS_PER_SEC;
#endif
}
}
std::string_view name(State s) { return states.at(static_cast<unsigned>(s)); }
std::string_view name(Scenario s) { return scenarios.at(static_cast<unsigned>(s)); }
bool can_transition(State from, State to) {
    if (to == State::cancelled) return from <= State::online;
    switch (from) {
    case State::created: return to == State::discovery;
    case State::discovery: return to == State::association || to == State::no_ap;
    case State::association: return to == State::authentication || to == State::rejected;
    case State::authentication: return to == State::dhcp || to == State::auth_denied;
    case State::dhcp: return to == State::dhcp_offer || to == State::no_dhcp || to == State::exhausted;
    case State::dhcp_offer: return to == State::dhcp_request;
    case State::dhcp_request: return to == State::dhcp_ack || to == State::conflict;
    case State::dhcp_ack: return to == State::configured;
    case State::configured: return to == State::gateway || to == State::bad_ip;
    case State::gateway: return to == State::dns || to == State::no_gateway;
    case State::dns: return to == State::online || to == State::no_dns;
    case State::online: return to == State::dhcp || to == State::interrupted;
    default: return false;
    }
}
bool Config::valid() const {
    return clients >= 1 && clients <= 50000 && pool >= 1 && pool <= 50000 && lease_ms >= 80 && lease_ms <= 60000 && static_cast<unsigned>(scenario) < static_cast<unsigned>(Scenario::count);
}
std::string serialize(const Config& c) {
    checked(c);
    return "CAMPUSLINK_SIM_V1 " + std::to_string(c.clients) + ' ' + std::to_string(c.pool) + ' ' +
        std::to_string(c.lease_ms) + ' ' + std::to_string(c.seed) + ' ' + std::to_string(static_cast<unsigned>(c.scenario));
}
Config parse(std::string_view text) {
    constexpr std::string_view prefix = "CAMPUSLINK_SIM_V1 ";
    if (!text.starts_with(prefix) || text.size() > 128) throw std::invalid_argument("Unknown or oversized simulation record");
    text.remove_prefix(prefix.size());
    std::array<unsigned, 5> values{};
    for (auto& value : values) {
        if (text.empty()) throw std::invalid_argument("Missing simulation value");
        const auto* end = text.data() + text.size();
        const auto parsed = std::from_chars(text.data(), end, value);
        if (parsed.ec != std::errc{} || (parsed.ptr != end && *parsed.ptr != ' ')) throw std::invalid_argument("Invalid simulation value");
        text.remove_prefix(static_cast<std::size_t>(parsed.ptr - text.data()));
        if (!text.empty()) text.remove_prefix(1);
    }
    if (!text.empty()) throw std::invalid_argument("Unexpected scenario values");
    return checked({values[0], values[1], values[2], values[3], static_cast<Scenario>(values[4])});
}
DhcpSimulator::DhcpSimulator(unsigned slots) : slots_(slots) {
    for (unsigned i = 1; i <= slots; ++i) free_.push(i);
}
unsigned DhcpSimulator::acquire(unsigned owner, unsigned now, unsigned duration) {
    if (!owner || free_.empty()) return 0;
    const auto slot = free_.front(); free_.pop();
    slots_[slot - 1] = {owner, now + duration};
    return slot;
}
bool DhcpSimulator::release(unsigned slot, unsigned owner) {
    if (!slot || slot > slots_.size() || !owner || slots_[slot - 1].owner != owner) return false;
    slots_[slot - 1] = {};
    free_.push(slot);
    return true;
}
bool DhcpSimulator::valid(unsigned slot, unsigned owner, unsigned now) const {
    return slot && slot <= slots_.size() && owner && slots_[slot - 1].owner == owner && now < slots_[slot - 1].expires;
}
bool Lab::Item::operator>(const Item& other) const { return std::tie(time, index) > std::tie(other.time, other.index); }
Lab::Lab(Config c) : config_(checked(c)), dhcp_(c.scenario == Scenario::pool ? std::min(c.pool, std::max(1u, c.clients / 2)) : c.pool),
    rng_(c.seed), clients_(c.clients) {
    for (unsigned i = 0; i < c.clients; ++i) {
        clients_[i].id = i + 1;
        clients_[i].history_size = 1;
        queue_.push({i % 17, i});
    }
}
void Lab::move(Client& client, State next) {
    if (!can_transition(client.state, next) || client.history_size == client.history.size()) throw std::logic_error("Invalid lab transition");
    client.state = next;
    client.history[client.history_size++] = {now_, next};
    ++events_;
}
void Lab::update(unsigned index) {
    auto& c = clients_[index];
    const auto s = config_.scenario;
    auto next = [&](State state, unsigned delay = 4) { move(c, state); queue_.push({now_ + delay, index}); };
    auto fail = [&](State state) { move(c, state); dhcp_.release(c.slot, c.id); c.slot = 0; };
    switch (c.state) {
    case State::created: next(State::discovery); break;
    case State::discovery: if (s == Scenario::no_ap) fail(State::no_ap); else next(State::association); break;
    case State::association: if (s == Scenario::rejected) fail(State::rejected); else next(State::authentication, s == Scenario::weak_signal ? 50 : 4); break;
    case State::authentication: if (s == Scenario::auth_denied) fail(State::auth_denied); else next(State::dhcp, s == Scenario::no_dhcp ? 100 : 4); break;
    case State::dhcp:
        if (s == Scenario::no_dhcp) fail(State::no_dhcp);
        else if (!(c.slot = dhcp_.acquire(c.id, now_, config_.lease_ms))) fail(State::exhausted);
        else next(State::dhcp_offer);
        break;
    case State::dhcp_offer: next(State::dhcp_request); break;
    case State::dhcp_request:
        if (s == Scenario::conflict && c.id % 2 == 0) fail(State::conflict);
        else { ++c.leases; next(State::dhcp_ack); }
        break;
    case State::dhcp_ack: next(State::configured); break;
    case State::configured: if (s == Scenario::bad_ip) fail(State::bad_ip); else next(State::gateway); break;
    case State::gateway: if (s == Scenario::no_gateway) fail(State::no_gateway); else next(State::dns, s == Scenario::slow_dns ? 300 : 4); break;
    case State::dns:
        if (s == Scenario::no_dns) { fail(State::no_dns); break; }
        c.metrics = {float(7 + rng_() % 17), float(1 + rng_() % 4), 0, float(40 + rng_() % 100), float(20 + rng_() % 40), -float(45 + rng_() % 20)};
        if (s == Scenario::weak_signal) { c.metrics.rssi_dbm = -float(78 + rng_() % 14); c.metrics.loss_percent = 8; c.metrics.down_mbps /= 3; }
        if (s == Scenario::congestion) { c.metrics.latency_ms += 100; c.metrics.jitter_ms += 12; c.metrics.down_mbps /= 8; c.metrics.up_mbps /= 4; }
        if (s == Scenario::jitter || s == Scenario::unstable) {
            c.metrics.latency_ms += 35; c.metrics.loss_percent = float(5 + rng_() % 25); c.metrics.jitter_ms += 30;
        }
        if (s == Scenario::expire && c.leases == 1) next(State::online, config_.lease_ms);
        else if (s == Scenario::unstable) next(State::online, 60);
        else move(c, State::online);
        break;
    case State::online:
        if (s == Scenario::expire) {
            if (dhcp_.valid(c.slot, c.id, now_)) throw std::logic_error("Expected expired lease");
            dhcp_.release(c.slot, c.id); c.slot = 0;
            next(State::dhcp);
        } else fail(State::interrupted);
        break;
    default: throw std::logic_error("Terminal client was scheduled");
    }
}
unsigned Lab::step(unsigned limit, std::stop_token stop) {
    const auto old = events_;
    while (limit && !done() && !stop.stop_requested()) {
        --limit;
        const auto item = queue_.top(); queue_.pop(); now_ = item.time; update(item.index);
    }
    return events_ - old;
}
void Lab::cancel() {
    while (!done()) {
        auto& c = clients_[queue_.top().index]; queue_.pop();
        if (can_transition(c.state, State::cancelled)) { move(c, State::cancelled); dhcp_.release(c.slot, c.id); c.slot = 0; }
    }
}
std::string Lab::csv() const {
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << serialize(config_) << "\nsource,client,simulated_ms,state,leases,latency_ms,jitter_ms,loss_percent,down_mbps,up_mbps,rssi_dbm\n";
    for (const auto& c : clients_) {
        for (unsigned i = 0; i < c.history_size; ++i) {
            const auto m = (i + 1 == c.history_size) ? c.metrics : Metrics{};
            out << "SIMULATED," << c.id << ',' << c.history[i].at_ms << ',' << name(c.history[i].state) << ','
                << (i + 1 == c.history_size ? c.leases : 0) << ',' << m.latency_ms << ',' << m.jitter_ms << ','
                << m.loss_percent << ',' << m.down_mbps << ',' << m.up_mbps << ',' << m.rssi_dbm << '\n';
        }
    }
    return out.str();
}
std::vector<Sample> profile(Budget budget, std::stop_token stop) {
    using Clock = std::chrono::steady_clock;
    if (!budget.max_clients || budget.max_clients > 50000 || !budget.memory_mib || !budget.wall_ms || !budget.cpu_ms) throw std::invalid_argument("Invalid profiler budget");
    const auto start = Clock::now(); const auto cpu_start = cpu_ms();
    const auto remaining = [&] { return !stop.stop_requested() && cpu_ms() - cpu_start < budget.cpu_ms &&
        Clock::now() - start < std::chrono::milliseconds(budget.wall_ms); };
    std::vector<Sample> samples;
    for (unsigned n = 1; remaining(); n = std::min(n * 2, budget.max_clients)) {
        // Accounted storage is a conservative per-client bound, not process RSS.
        const std::uint64_t bytes = std::uint64_t{n} * (sizeof(Client) + 128U);
        if (bytes > std::uint64_t{budget.memory_mib} * 1024 * 1024) break;
        const auto begin = Clock::now(); const auto cpu = cpu_ms();
        Lab lab({n, n, 250, 42, Scenario::healthy});
        std::vector<double> batches;
        while (!lab.done() && remaining()) {
            const auto before = Clock::now(); lab.step(128, stop);
            batches.push_back(std::chrono::duration<double, std::micro>(Clock::now() - before).count());
        }
        std::sort(batches.begin(), batches.end());
        samples.push_back({n, lab.events(), std::chrono::duration<double, std::milli>(Clock::now() - begin).count(),
            cpu_ms() - cpu, batches.empty() ? 0.0 : batches[(batches.size() - 1) * 95 / 100], bytes, lab.done() && remaining()});
        if (!samples.back().within_budget || n == budget.max_clients) break;
    }
    return samples;
}
}
