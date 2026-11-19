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
    std::string id, name, description, kind, hardware_id, manufacturer, driver, provider, driver_date;
    std::string address, gateway, dns;
    std::string ssid, bssid, profile, phy, radio;
    std::string device_instance, active_role, documented_radio, current_phy, wlan_interface_type;
    std::optional<unsigned> channel;
    std::optional<Error> secondary_sta_query;
    unsigned secondary_sta_bytes{};
    std::optional<unsigned> desired_bssids;
    std::optional<bool> mfp_capable, dot11d_capable, hosted_network_capable;
    bool hardware_interface{}, secondary_sta_reported{};
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
struct Distribution {
    unsigned count{};
    std::optional<double> mean, median, p95, minimum, maximum;
};
struct TransferSeries {
    unsigned bytes{}, attempts{};
    std::vector<double> up, down;
};
Distribution summarize(std::vector<double>);
struct Observation {
    std::optional<double> stability, repeatability, down_mbps, latency_ms, loss_percent, jitter_ms, up_mbps, dns_ms;
    unsigned attempts{}, replies{}, bytes{};
    bool gateway_replied{}, endpoint_verified{};
    std::vector<float> latency_history;
    std::optional<Error> error;
    std::vector<TransferSeries> transfers;
    std::vector<double> dns_history;
    Distribution rtt_distribution, dns_distribution;
    bool sample_complete{};
};
void summarize_observation(Observation&);
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
    std::string method{};
    Clock::time_point measurement_start{};
    bool host_validated{};
};
Score candidate_score(const Candidate&, std::array<double, 5> weights = base_weights);
std::vector<Candidate> rank(std::vector<Candidate> candidates, Clock::time_point now);
bool should_switch(const Candidate&, const Candidate&, Clock::time_point now, Clock::time_point last_switch);
struct Scope {
    std::string profile, bssid, endpoint, dns_name;
    unsigned port{}, repetitions{}, bytes{}, timeout_ms{}, cooldown_s{};
    bool rollback{};
    std::vector<unsigned> sizes;
    unsigned transfer_repetitions{2}, settle_ms{400};
    bool require_gateway_reply{};
    std::string method() const;
    std::vector<unsigned> transfer_sizes() const;
    bool valid() const;
    bool allows(const Bss&) const;
};
Scope parse_scope(std::string_view);
std::vector<Scope> parse_scopes(std::string_view);
struct Experiment { Scope scope; Bss candidate; };
std::vector<std::vector<std::size_t>> experiment_waves(const std::vector<Experiment>&, const Snapshot&);
bool usable_ipv4(std::string_view);
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
std::optional<Error> loopback_protocol(unsigned port);
}
