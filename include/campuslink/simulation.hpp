#pragma once

#include <array>
#include <cstdint>
#include <queue>
#include <random>
#include <stop_token>
#include <string>
#include <string_view>
#include <vector>

namespace campuslink::sim {
enum class Scenario { healthy, pool, no_dhcp, expire, bad_ip, no_gateway, slow_dns, no_dns,
    weak_signal, congestion, jitter, unstable, conflict, no_ap, rejected, auth_denied, count };
enum class State { created, discovery, association, authentication, dhcp, dhcp_offer, dhcp_request, dhcp_ack, configured, gateway, dns,
    online, interrupted, no_ap, rejected, auth_denied, no_dhcp, exhausted, conflict, bad_ip,
    no_gateway, no_dns, cancelled, count };
std::string_view name(State);
std::string_view name(Scenario);
bool can_transition(State from, State to);
struct Config {
    unsigned clients{100}, pool{128}, lease_ms{250}, seed{42};
    Scenario scenario{Scenario::healthy};
    bool valid() const;
    bool operator==(const Config&) const = default;
};
std::string serialize(const Config&);
Config parse(std::string_view);
struct Event { unsigned at_ms; State state; };
struct Metrics { float latency_ms{}, jitter_ms{}, loss_percent{}, down_mbps{}, up_mbps{}, rssi_dbm{}; };
struct Client {
    unsigned id{};
    State state{State::created};
    unsigned slot{}; // logical pool index; never a host address
    unsigned leases{}, discovers{}, expires_at{};
    Metrics metrics;
    std::array<Event, 24> history{};
    unsigned history_size{};
};
class DhcpSimulator {
    struct Lease { unsigned owner{}, expires{}; };
    std::vector<Lease> slots_;
    std::queue<unsigned> free_;
public:
    explicit DhcpSimulator(unsigned slots);
    unsigned acquire(unsigned owner, unsigned now, unsigned duration);
    bool release(unsigned slot, unsigned owner);
    bool valid(unsigned slot, unsigned owner, unsigned now) const;
    bool commit(unsigned slot, unsigned owner, unsigned now, unsigned duration);
};
class Lab {
    struct Item {
        unsigned time, index;
        bool operator>(const Item& other) const;
    };
    Config config_;
    DhcpSimulator dhcp_;
    std::mt19937 rng_;
    std::priority_queue<Item, std::vector<Item>, std::greater<Item>> queue_;
    std::vector<Client> clients_;
    unsigned now_{};
    unsigned events_{};
    void update(unsigned index);
    void move(Client&, State);
public:
    explicit Lab(Config);
    unsigned step(unsigned limit, std::stop_token stop = {});
    void cancel();
    unsigned now() const { return now_; }
    unsigned events() const { return events_; }
    bool done() const { return queue_.empty(); }
    const std::vector<Client>& clients() const { return clients_; }
    std::string csv() const;
};
struct Budget { unsigned max_clients{10000}, memory_mib{32}, wall_ms{1500}, cpu_ms{900}; };
enum class ProfileEnd { limit_reached, memory_budget, cpu_budget, wall_budget, cancelled };
std::string_view name(ProfileEnd);
struct Sample {
    unsigned clients{}, events{};
    double elapsed_ms{}, cpu_ms{}, p95_us{};
    std::uint64_t baseline_rss{}, sampled_peak_rss{}, sampled_growth{}, accounted_bytes{};
    bool within_budget{};
};
struct Profile {
    std::vector<Sample> samples;
    ProfileEnd end{ProfileEnd::cancelled};
    unsigned completed_clients() const;
};
Profile profile(Budget, std::stop_token = {});
}
