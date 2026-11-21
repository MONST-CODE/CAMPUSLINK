#pragma once

#include <array>
#include <chrono>
#include <functional>
#include <optional>
#include <stop_token>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace campuslink::net {
using Clock = std::chrono::steady_clock;
// Coarse failure class. It decides what the assistant may try next:
// only radio/association problems justify another access point of the same network.
enum class WifiFailure {
    unspecified,
    request,                 // Windows refused the request (WlanConnect error)
    association_unconfirmed, // no confirmed association, cause not reported
    ip_unconfirmed,          // associated, IPv4/gateway not yet usable
    authentication,          // security/802.1X negotiation failed (wrong key, EAP, ...)
    profile,                 // saved profile unusable or incompatible with this network
    radio,                   // access point not reachable / association rejected or timed out
};
// True when another BSSID of the same network can reasonably change the outcome.
constexpr bool retry_elsewhere(WifiFailure f) {
    return f == WifiFailure::association_unconfirmed || f == WifiFailure::radio;
}
struct Error {
    std::string operation; unsigned code{}; std::string detail; bool request_unsettled{}; WifiFailure wifi_failure{};
    unsigned wlan_reason{};   // WLAN_REASON_CODE reported by Windows, 0 if none
    std::string os_message{}; // WlanReasonCodeToString text in the Windows display language
};
// Map a Windows WLAN_REASON_CODE (documented L2 reason ranges) to a failure class.
WifiFailure classify_wlan_reason(unsigned reason);
// Wi-Fi SSIDs are raw octets. The scanner stores them as printable ASCII with \xNN escapes
// so they round-trip into WlanConnect. For display, decode the escapes and keep valid UTF-8.
std::string display_ssid(std::string_view escaped);
struct SecondarySta { std::string id; unsigned state{}; };
struct Interface {
    std::string id, name, description, kind, hardware_id, manufacturer, driver, provider, driver_date;
    std::string address, gateway, dns;
    std::string ssid, bssid, profile, phy, radio;
    std::string device_instance, active_role, documented_radio, current_phy, wlan_interface_type;
    std::optional<unsigned> channel;
    std::optional<Error> secondary_sta_query;
    std::vector<SecondarySta> secondary_sta;
    std::optional<bool> secondary_synchronized;
    unsigned secondary_sta_bytes{};
    std::optional<unsigned> desired_bssids;
    std::optional<bool> mfp_capable, dot11d_capable, hosted_network_capable;
    bool hardware_interface{}, secondary_sta_reported{}, wlan_idle{};
    unsigned index{};
    bool wireless{}, up{}, associated{}, dhcp{};
    double rx_mbps{}, tx_mbps{};
    std::optional<int> rssi;
};
struct Bss {
    std::string interface_id, ssid, bssid, security, profile;
    int rssi{};
    unsigned mhz{}, channel{};
    bool infrastructure{true};
};
struct Snapshot { std::vector<Interface> interfaces; std::vector<Error> errors; };
// cached: Windows did not confirm a fresh scan for at least one adapter; its recent list was used.
// stale_dropped: entries ignored because Windows last heard them more than two minutes ago.
struct Discovery { std::vector<Bss> bss; std::vector<Error> errors; bool cached{}; unsigned stale_dropped{}; };
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
// Windows-reported evidence for one adapter (Network List Manager / NCSI).
// It is not an active Internet test run by CAMPUSLINK.
enum class Reachability { unknown, local_only, internet, portal };
enum class WifiMode { normal, assisted };
struct WifiTiming {
    std::optional<std::chrono::milliseconds> association, usable, internet_reported;
};
// Engine-confirmed progress. The UI shows these stages; it never invents one.
enum class WifiStage { preparing, associating, obtaining_address, checking_internet, restoring };
struct WifiProgress {
    WifiStage stage{WifiStage::preparing};
    unsigned attempt{}, attempts{};  // 1-based attempt index within the plan
    std::string bssid;
};
using WifiProgressSink = std::function<void(const WifiProgress&)>;
// Bounded native waits. Association covers authentication; address covers DHCP and
// gateway stability once the association is confirmed.
struct WifiBudget {
    std::chrono::milliseconds association{std::chrono::seconds(25)};
    std::chrono::milliseconds address{std::chrono::seconds(40)};
    std::chrono::milliseconds internet{std::chrono::seconds(12)};
};
struct Backend {
    virtual ~Backend() = default;
    virtual Snapshot inspect(std::stop_token) = 0;
    virtual Discovery scan(std::stop_token) = 0;
    virtual std::optional<Error> connect(const Bss&, std::stop_token) = 0;
    virtual std::optional<Error> connect_profile(const Bss&, std::stop_token) = 0;
    // Request one association. On ip_unconfirmed the association is left in place:
    // Windows keeps negotiating the address and the caller decides what to report.
    virtual std::optional<Error> request_wifi(const Bss& b, WifiMode mode, std::stop_token stop, WifiTiming&,
                                              const WifiBudget& = {}, const WifiProgressSink& = {}) {
        return mode == WifiMode::assisted ? connect(b, stop) : connect_profile(b, stop);
    }
    virtual Reachability reachability(const Interface&) { return Reachability::unknown; }
    virtual Observation measure(const Interface&, const Scope&, std::stop_token) = 0;
};
struct Handoff { std::string state; bool rolled_back{}; std::optional<Error> error; std::optional<Candidate> measured; };
Handoff select(Backend&, const Scope&, const Bss&, std::stop_token);
// Authorized active measurement of a current association. Never requests a link change.
Handoff observe_path(Backend&, const Scope&, std::string_view interface_id, std::stop_token);
// ip_pending: associated, Windows still negotiating IPv4; the association is kept.
// portal: Windows reports a captive portal (web sign-in) on the connected adapter.
enum class WifiStatus { invalid, cancelled, failed, recovery_failed, request_unsettled, local_link, internet_reported, ip_pending, portal };
inline constexpr unsigned wifi_max_attempts = 4;
inline constexpr unsigned wifi_default_attempts = 3;
inline constexpr unsigned wifi_sweep_cap = 6; // bound a full same-SSID sweep so total time stays bounded
inline constexpr auto wifi_scan_ttl = std::chrono::minutes(2);
struct WifiHistory {
    Bss candidate;
    Clock::time_point observed;
    bool success{};
};
// Per-network memory (ADR 0004): what worked for one network identity (SSID + security).
// Persisted locally, user-clearable. It is NOT a location: two networks that share a name and
// security are merged. It biases candidate selection toward known-good evidence, never fabricates.
struct NetworkRecord {
    std::string ssid, security, best_bssid;
    unsigned successes{}, failures{};
    bool portal_seen{};
    std::optional<unsigned> assoc_ms, dhcp_ms; // smoothed last observed timings, for display
    long long last_seen{};                     // epoch seconds, for display and pruning
};
inline bool record_trusts(const NetworkRecord& r, std::string_view bssid) {
    return !r.best_bssid.empty() && r.best_bssid == bssid && r.successes > r.failures;
}
struct WifiPolicy {
    unsigned max_attempts{wifi_default_attempts};
    Clock::time_point scanned_at{}; // When provided, the scan must be fresh when the operation starts.
    std::span<const WifiHistory> history{};
    WifiBudget budget{};
    WifiProgressSink progress{};
    std::optional<NetworkRecord> memory{}; // known-good evidence for the selected network, if any
    bool sweep{};                           // try every saved access point of the SSID, not just a few
};
// known_good: this exact access point reached a usable link before (persistent memory).
// less_congested: equal signal, fewer other access points share the channel (AP density, not load).
enum class WifiReason { selected, independent_idle, previous_success, avoid_recent_failure, signal, known_good, less_congested };
struct WifiAttempt {
    Bss candidate;
    std::optional<Error> error;
    WifiTiming timing;
    WifiReason reason{};
};
struct WifiResult {
    WifiStatus status{WifiStatus::invalid};
    std::optional<Interface> link;
    std::vector<WifiAttempt> attempts;
    Reachability evidence{Reachability::unknown};
    bool restored{}, recovery_attempted{};
    Clock::time_point finished{};
    std::chrono::milliseconds elapsed{};
};
// Explainable pre-selection of the assisted starting access point among saved, infrastructure
// candidates. 5/6 GHz gets a 6 dB preference when its reported signal is at least -72 dBm (less
// crowded spectrum, wider channels); otherwise the strongest reported signal wins. This is a
// heuristic on scan data, not a throughput measurement.
// known_good: the network's remembered best access point is in range with adequate signal.
enum class StartReason { strongest, wider_band, known_good };
struct StartChoice { std::size_t index{}; StartReason reason{}; };
std::optional<StartChoice> recommend_start(std::span<const Bss> candidates, const NetworkRecord* memory = nullptr);
// Number of other access points sharing each candidate's channel in the same scan. A coarse
// density proxy (not airtime or load), used only as a deterministic tiebreak between equal signals.
unsigned channel_density(const Discovery&, const Bss&);
std::vector<Bss> wifi_plan(const Bss& selected, const Discovery&, const Snapshot&, WifiMode, WifiPolicy = {}, Clock::time_point = Clock::now());
WifiResult connect_wifi(Backend&, const Bss& selected, const Discovery&, WifiMode, std::stop_token, WifiPolicy = {});
// Propose (never perform) moving the current association to a clearly stronger access point of the
// same saved network. Returns a suggestion only when the active link is at or below weak_floor_dbm
// AND an eligible access point improves RSSI by at least min_gain_db (hysteresis). The caller proposes.
struct RoamSuggestion { Bss target; int from_rssi{}; int to_rssi{}; };
std::optional<RoamSuggestion> suggest_roam(const Interface& active, const Discovery&, const Snapshot&,
                                           int weak_floor_dbm, int min_gain_db);
Backend& platform();
std::optional<Error> loopback_protocol(unsigned port);
}
