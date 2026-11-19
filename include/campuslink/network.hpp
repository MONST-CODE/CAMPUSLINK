#pragma once

#include <array>
#include <chrono>
#include <optional>
#include <stop_token>
#include <string>
#include <string_view>
#include <vector>

namespace campuslink::net {
using Clock = std::chrono::steady_clock;
struct Error { std::string operation; unsigned code{}; std::string detail; };
struct Interface {
    std::string id, name, description, kind, hardware_id, driver, provider, driver_date;
    std::string address, gateway, dns;
    std::string ssid, bssid, profile, phy;
    unsigned index{};
    bool wireless{}, up{}, associated{}, dhcp{};
    double rx_mbps{}, tx_mbps{};
    std::optional<int> rssi;
};
struct Bss {
    std::string interface_id, ssid, bssid, security, profile;
    int rssi{};
    unsigned mhz{}, channel{};
};
struct Snapshot { std::vector<Interface> interfaces; std::vector<Error> errors; };
struct Discovery { std::vector<Bss> bss; std::vector<Error> errors; };
struct Observation {
    std::optional<double> stability, down_mbps, latency_ms, loss_percent, jitter_ms, up_mbps, dns_ms;
    unsigned attempts{}, replies{}, bytes{};
    bool gateway_replied{}, endpoint_verified{};
    std::vector<float> latency_history;
    std::optional<Error> error;
};
struct Score {
    std::array<std::optional<double>, 5> components;
    double points{}, coverage{};
    unsigned samples{};
    bool eligible{};
};
inline constexpr std::array base_weights{30., 25., 20., 15., 10.};
Score score(const Observation&, std::array<double, 5> weights = base_weights);
struct Candidate {
    Bss bss;
    Observation observation;
    Score rating;
    Clock::time_point observed;
};
std::vector<Candidate> rank(std::vector<Candidate> candidates, Clock::time_point now);
bool should_switch(const Candidate&, const Candidate&, Clock::time_point now, Clock::time_point last_switch);
struct Scope {
    std::string profile, bssid, endpoint;
    unsigned port{}, repetitions{}, bytes{}, timeout_ms{}, cooldown_s{};
    bool rollback{};
    bool valid() const;
    bool allows(const Bss&) const;
};
Scope parse_scope(std::string_view);
struct Diagnostic { std::string step, status, detail; };
std::vector<Diagnostic> diagnose(const Interface*, bool detected, const Observation*);
struct Backend {
    virtual ~Backend() = default;
    virtual Snapshot inspect(std::stop_token) = 0;
    virtual Discovery scan(std::stop_token) = 0;
    virtual std::optional<Error> connect(const Bss&, std::stop_token) = 0;
    virtual Observation measure(const Interface&, const Scope&, std::stop_token) = 0;
};
struct Handoff { std::string state; bool rolled_back{}; std::optional<Error> error; std::optional<Candidate> measured; };
Handoff select(Backend&, const Scope&, const Bss&, std::stop_token);
Backend& platform();
}
