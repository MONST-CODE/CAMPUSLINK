#include "campuslink/network.hpp"
#include "campuslink/dns.hpp"
#include "campuslink/simulation.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <functional>
#include <iostream>
#include <memory>
#include <stdexcept>

namespace {
using namespace campuslink;
using namespace std::chrono_literals;
unsigned passed{}, failed{};
void require(bool value, const char* why) { if (!value) throw std::runtime_error(why); }
template<class F> void check(std::string_view name, F f) {
    try { f(); ++passed; std::cout << "PASS " << name << '\n'; }
    catch (const std::exception& e) { ++failed; std::cerr << "FAIL " << name << ": " << e.what() << '\n'; }
}
template<class F> bool rejected(F f) {
    try { f(); return false; } catch (const std::invalid_argument&) { return true; }
}
const std::string scope_text = "version=1\nprofile=Authorized lab\nbssid=02:00:00:00:00:02\nendpoint=192.0.2.3\nport=8123\nrepetitions=4\nbytes=2048\ntimeout_ms=800\ncooldown_s=30\nrollback=true\n";
const net::Bss target{"wifi0", "Lab simulated", "02:00:00:00:00:02", "Protected", "Authorized lab"};
net::Observation good() {
    net::Observation o;
    o.stability = 100.; o.down_mbps = 90.; o.latency_ms = 10.; o.loss_percent = 0.; o.jitter_ms = 4.;
    o.attempts = o.replies = 4; o.endpoint_verified = true;
    return o;
}
struct Mock final : net::Backend {
    net::Interface interface;
    bool absent{}, failure{}, connect_fail{}, rollback_fail{};
    unsigned connections{}, scans{}, probes{};
    Mock() {
        interface.id = "wifi0"; interface.up = interface.associated = interface.wireless = true; interface.kind = "Physical (reported)";
        interface.profile = "Previous"; interface.bssid = "02:00:00:00:00:01";
        interface.address = "192.0.2.102"; interface.gateway = "192.0.2.1"; interface.dns = "192.0.2.5";
    }
    net::Snapshot inspect(std::stop_token) override { return absent ? net::Snapshot{} : net::Snapshot{{interface}, {}}; }
    net::Discovery scan(std::stop_token) override { ++scans; auto second = target; second.bssid.back() = '3'; return {{target, second}, {}}; }
    std::optional<net::Error> connect(const net::Bss& b, std::stop_token) override {
        ++connections;
        if ((connect_fail && connections == 1) || (rollback_fail && connections == 2)) return net::Error{"connect", 1460, "Synthetic timeout"};
        interface.profile = b.profile; interface.bssid = b.bssid; return {};
    }
    net::Observation measure(const net::Interface&, const net::Scope&, std::stop_token) override {
        ++probes; auto o = good(); o.endpoint_verified = !failure; return o;
    }
};
}
int main() {
    using sim::Scenario; using sim::State;
    for (const auto& [s, expected] : std::array{
        std::pair{Scenario::healthy, State::online}, std::pair{Scenario::pool, State::exhausted},
        std::pair{Scenario::no_dhcp, State::no_dhcp}, std::pair{Scenario::expire, State::online},
        std::pair{Scenario::bad_ip, State::bad_ip}, std::pair{Scenario::no_gateway, State::no_gateway},
        std::pair{Scenario::slow_dns, State::online}, std::pair{Scenario::no_dns, State::no_dns},
        std::pair{Scenario::weak_signal, State::online}, std::pair{Scenario::congestion, State::online},
        std::pair{Scenario::jitter, State::online}, std::pair{Scenario::unstable, State::interrupted},
        std::pair{Scenario::conflict, State::conflict}, std::pair{Scenario::no_ap, State::no_ap},
        std::pair{Scenario::rejected, State::rejected}, std::pair{Scenario::auth_denied, State::auth_denied}}) {
        check(sim::name(s), [&] {
            sim::Lab a({32, 32, 160, 17, s}), b({32, 32, 160, 17, s});
            while (!a.done()) a.step(7);
            while (!b.done()) b.step(128);
            require(a.csv() == b.csv(), "deterministic replay across batch sizes");
            require(std::any_of(a.clients().begin(), a.clients().end(), [&](const auto& c) { return c.state == expected; }), "expected terminal state");
            if (s == Scenario::healthy) {
                const auto& c = a.clients().front();
                auto seen = [&](State state) { return std::any_of(c.history.begin(), c.history.begin() + c.history_size, [&](const auto& e) { return e.state == state; }); };
                require(seen(State::dhcp_offer) && seen(State::dhcp_request) && seen(State::dhcp_ack), "DHCP transition sequence missing");
            }
            if (s == Scenario::weak_signal) require(a.clients().front().metrics.rssi_dbm < -70, "signal scenario did not affect RSSI");
            if (s == Scenario::expire) require(a.clients().front().leases == 2, "lease renewed");
            if (s == Scenario::slow_dns) require(a.now() >= 300, "DNS delay");
            if (s == Scenario::jitter) require(a.clients().front().metrics.jitter_ms > 10, "jitter not observed");
        });
    }
    check("transition contract", [] { require(sim::can_transition(State::created, State::discovery) && !sim::can_transition(State::created, State::online), "wrong transition table"); });
    check("pool capacity and lease ownership/expiry", [] {
        sim::DhcpSimulator leases(1);
        require(leases.acquire(1, 100, 20) == 1 && leases.acquire(2, 100, 20) == 0, "exclusive lease");
        require(!leases.release(1, 2) && leases.valid(1, 1, 119) && !leases.valid(1, 1, 120), "ownership or expiry");
        require(leases.release(1, 1) && leases.acquire(2, 120, 20), "pool reclaimed");
    });
    check("scheduler cancellation and 10k clients", [] {
        sim::Lab a({10000, 10000, 250, 42, Scenario::healthy});
        require(a.step(128) == 128, "step budget");
        auto source = std::make_unique<std::stop_source>(); source->request_stop();
        require(a.step(128, source->get_token()) == 0, "cancellation not honoured");
        a.cancel(); require(a.done() && a.clients().back().state == State::cancelled, "pending clients not cancelled");
    });
    check("scenario codec", [] {
        const sim::Config c{100, 8, 80, 17, Scenario::expire};
        require(sim::parse(sim::serialize(c)) == c, "scenario round trip");
        require(rejected([] { sim::parse("CAMPUSLINK_SIM_V1 0 1 80 42 0"); }), "invalid clients accepted");
        require(rejected([] { sim::parse("CAMPUSLINK_SIM_V1 1 1 80 42 999 garbage"); }), "trailing tokens accepted");
    });
    check("capacity profiler budgets", [] {
        auto data = sim::profile({100, 1, 1500, 1000});
        require(!data.empty() && data.back().clients == 100 && data.back().within_budget, "capacity sample missing");
        auto token = std::make_unique<std::stop_source>(); token->request_stop();
        require(sim::profile({}, token->get_token()).empty(), "capacity cancellation failed");
    });
    check("score bounded, missingness and eligibility", [] {
        auto o = good(); auto rating = net::score(o);
        require(std::abs(rating.points - 96.) < .001 && rating.eligible, "unexpected weighted result");
        o.down_mbps.reset(); o.jitter_ms.reset(); rating = net::score(o);
        require(!rating.eligible && rating.coverage == .65 && !rating.components[1], "missingness not preserved");
        require(rejected([&] { net::score(o, {0., 0., 0., 0., 0.}); }), "all-zero weights accepted");
    });
    check("ranking freshness, cooldown and hysteresis", [] {
        const auto now = net::Clock::now();
        auto a = net::Candidate{target, good(), net::score(good()), now - 1min};
        auto b = a; b.bss.bssid.back() = '3'; a.rating.points = 65; b.rating.points = 80;
        require(net::should_switch(a, b, now, now - 2min) && !net::should_switch(a, b, now, now - 5s), "cooldown/hysteresis failed");
        b.observed -= 6min;
        require(net::rank({b, a}, now).front().bss.bssid == a.bss.bssid, "stale candidate won");
    });
    check("scope strict validation", [] {
        const auto s = net::parse_scope(scope_text);
        require(s.valid() && s.allows(target), "configured scope disallowed");
        require(rejected([] { net::parse_scope(scope_text + "port=1\n"); }), "duplicate accepted");
        auto bigger = scope_text; bigger.replace(bigger.find("2048"), 4, "999999");
        require(rejected([&] { net::parse_scope(bigger); }), "over-volume allowed");
    });
    check("bounded DNS reply validation", [] {
        auto query = net::dns_query("bench.lab.example", 0xbeef);
        auto reply = query; reply[2] = 0x81; reply[3] = 0x80; reply[7] = 1;
        for (auto v : {192,12,0,1,0,1,0,0,0,20,0,4,192,0,2,3}) reply.push_back(static_cast<std::uint8_t>(v));
        require(net::dns_confirms(query, reply, {192,0,2,3}), "valid compressed DNS response rejected");
        require(!net::dns_confirms(query, reply, {192,0,2,4}), "wrong endpoint accepted");
        reply.pop_back(); require(!net::dns_confirms(query, reply, {192,0,2,3}), "truncated response accepted");
        require(rejected([] { net::parse_scope(scope_text + "dns_name=not a name\n"); }), "invalid name accepted");
    });
    check("discovery preserves multiple BSS per SSID; APIPA stays hypothesis", [] {
        Mock mock; const auto d = mock.scan({});
        require(d.bss.size() == 2 && d.bss[0].ssid == d.bss[1].ssid && d.bss[0].bssid != d.bss[1].bssid, "BSS identity collapsed");
        mock.interface.address = "169.254.1.1";
        const auto entries = net::diagnose(&mock.interface, true, nullptr);
        require(entries.size() == 9 && entries[4].status == "WARN" && entries[8].status == "UNVERIFIED", "diagnostics must not infer Internet");
    });
    check("authorized handoff and post-connect validation", [] {
        Mock mock; const auto h = net::select(mock, net::parse_scope(scope_text), target, {});
        require(h.state == "HOST_VALIDATED" && h.measured && mock.connections == 1 && mock.probes == 1, "expected selected and validated");
    });
    check("scope rejection, absent adapter, cancellation make no mutation", [] {
        Mock mock; const auto s = net::parse_scope(scope_text);
        auto blocked = target; blocked.bssid.back() = '4';
        require(net::select(mock, s, blocked, {}).state == "SCOPE_BLOCKED", "unscoped mutation");
        mock.absent = true; require(net::select(mock, s, target, {}).state == "ADAPTER_UNAVAILABLE", "missing adapter accepted");
        auto stop = std::make_unique<std::stop_source>(); stop->request_stop(); require(net::select(mock, s, target, stop->get_token()).state == "CANCELLED", "stop ignored");
        require(mock.connections == 0 && mock.probes == 0, "side effect despite gate");
    });
    check("one rollback when endpoint verification fails", [] {
        Mock mock; mock.failure = true;
        const auto h = net::select(mock, net::parse_scope(scope_text), target, {});
        require(h.rolled_back && mock.connections == 2 && mock.interface.profile == "Previous", "previous profile not restored");
    });
    check("failed connection and failed rollback are bounded and preserve timeout", [] {
        Mock mock; mock.connect_fail = mock.rollback_fail = true;
        const auto h = net::select(mock, net::parse_scope(scope_text), target, {});
        require(h.state == "ROLLBACK_FAILED" && h.error && h.error->code == 1460 && mock.connections == 2 && !mock.probes, "bounded rollback failure not reported");
    });
    std::cout << "SUMMARY " << passed << " passed, " << failed << " failed\n";
    return static_cast<int>(failed != 0);
}
