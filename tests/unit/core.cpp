#include "campuslink/network.hpp"
#include "campuslink/path_lab.hpp"
#include "campuslink/ui_text.hpp"
#include "campuslink/dns.hpp"
#include "campuslink/simulation.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <functional>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <thread>

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
    bool absent{}, failure{}, connect_fail{}, rollback_fail{}, rollback_wrong_route{};
    unsigned connections{}, scans{}, probes{};
    bool roam_in_measure{};
    std::stop_source* cancel_in_measure{};
    Mock() {
        interface.id = "wifi0"; interface.up = interface.associated = interface.wireless = true; interface.kind = "Physical (reported)";
        interface.profile = "Previous"; interface.bssid = "02:00:00:00:00:01";
        interface.address = "192.0.2.102"; interface.gateway = "192.0.2.1"; interface.dns = "192.0.2.5";
    }
    net::Snapshot inspect(std::stop_token) override { return absent ? net::Snapshot{} : net::Snapshot{{interface}, {}}; }
    net::Discovery scan(std::stop_token) override { ++scans; auto second = target; second.bssid.back() = '3'; return {{target, second}, {}}; }
    std::optional<net::Error> connect(const net::Bss& b, std::stop_token stop) override {
        require(!stop.stop_requested(), "cleanup received a cancelled token");
        ++connections;
        if ((connect_fail && connections == 1) || (rollback_fail && connections == 2)) return net::Error{"connect", 1460, "Synthetic timeout"};
        interface.profile = b.profile; interface.bssid = b.bssid;
        if (rollback_wrong_route && connections == 2) interface.gateway.clear();
        return {};
    }
    std::optional<net::Error> connect_profile(const net::Bss& b, std::stop_token stop) override { return connect(b, stop); }
    net::Observation measure(const net::Interface&, const net::Scope&, std::stop_token) override {
        ++probes; if (roam_in_measure) interface.bssid = "02:00:00:00:00:03"; if (cancel_in_measure) cancel_in_measure->request_stop();
        auto o = good(); o.endpoint_verified = !failure; return o;
    }
};
struct WifiMock final : net::Backend {
    net::Snapshot topology;
    net::Reachability report{net::Reachability::unknown};
    std::vector<net::Reachability> reports;  // consumed first, then `report`
    unsigned normal{}, pinned{}, probes{}, reach_calls{};
    bool fail_first{}, fail_all{}, fail_recovery{}, alternate_on_normal{}, unsettled{}, no_address{};
    net::WifiFailure failure_class{net::WifiFailure::association_unconfirmed};  // as classified by the native backend
    std::chrono::milliseconds delay{};
    std::vector<std::string> interfaces;
    std::vector<net::WifiStage> stages;
    net::Snapshot inspect(std::stop_token) override { return topology; }
    net::Discovery scan(std::stop_token) override { return {}; }
    std::optional<net::Error> apply(const net::Bss& b, bool exact) {
        interfaces.push_back(b.interface_id);
        if (delay.count()) std::this_thread::sleep_for(delay);
        auto it = std::find_if(topology.interfaces.begin(), topology.interfaces.end(), [&](const auto& i) { return i.id == b.interface_id; });
        if (it == topology.interfaces.end()) return net::Error{"mock", 1, "missing"};
        if ((fail_first && interfaces.size() == 1) || (fail_all && b.profile != "Previous") || (fail_recovery && b.profile == "Previous")) {
            it->associated = false;
            return net::Error{"mock", 1460, "timeout", unsettled, failure_class};
        }
        it->associated = it->up = true;
        it->ssid = b.ssid;
        it->profile = b.profile;
        it->bssid = !exact && alternate_on_normal ? "02:00:00:00:00:10" : b.bssid;
        if (no_address && b.profile != "Previous") {
            it->address.clear(); it->gateway.clear();
            return net::Error{"DHCP", 1460, "no address", false, net::WifiFailure::ip_unconfirmed};
        }
        it->address = "192.0.2.100";
        it->gateway = "192.0.2.1";
        return {};
    }
    std::optional<net::Error> connect(const net::Bss& b, std::stop_token) override { ++pinned; return apply(b, true); }
    std::optional<net::Error> connect_profile(const net::Bss& b, std::stop_token) override { ++normal; return apply(b, false); }
    std::optional<net::Error> request_wifi(const net::Bss& b, net::WifiMode mode, std::stop_token stop, net::WifiTiming&,
                                           const net::WifiBudget&, const net::WifiProgressSink& progress) override {
        auto e = mode == net::WifiMode::assisted ? connect(b, stop) : connect_profile(b, stop);
        if ((!e || e->wifi_failure == net::WifiFailure::ip_unconfirmed) && progress) progress({net::WifiStage::obtaining_address, 0, 0, b.bssid});
        return e;
    }
    net::Reachability reachability(const net::Interface&) override {
        ++reach_calls;
        if (!reports.empty()) { const auto r = reports.front(); reports.erase(reports.begin()); return r; }
        return report;
    }
    net::Observation measure(const net::Interface&, const net::Scope&, std::stop_token) override { ++probes; return {}; }
};
net::Interface wifi_interface(std::string id, std::string device) {
    net::Interface i; i.wireless = i.hardware_interface = i.wlan_idle = true; i.id = std::move(id); i.device_instance = std::move(device); return i;
}

}
int main() {
    using sim::Scenario; using sim::State;
    check("bilingual UI catalog, composed diagnostics and French UTF-8", [] {
        using namespace campuslink::ui;
        require(catalog_complete() && catalog.size() >= 350, "incomplete UI translation catalog");
        require(std::string_view(translate(Language::fr, Key::About)) == "À propos" &&
            std::string_view(translate(Language::en, Key::About)) == "About", "language switch or Unicode labels invalid");
        require(localize(Language::fr, "Hardware inventory running") == "Inventaire matériel en cours", "worker state did not translate");
        require(localize(Language::fr, "Gateway did not answer; required by operator scope; rollback failed: WlanConnect / 31").ends_with(" ; retour arrière en échec : WlanConnect / 31"),
            "native diagnostic fragments were not localized or lost status codes");
        for (const auto& d : net::diagnose(nullptr, false, nullptr))
            require(localize(Language::fr, d.detail) != d.detail, "untranslated diagnostic");
    });
    check("display settings round trip, overwrite and corruption fallback", [] {
        using namespace campuslink::ui;
        const auto stamp = std::to_string(net::Clock::now().time_since_epoch().count());
        struct Cleanup { std::filesystem::path path; ~Cleanup() { std::error_code ec; std::filesystem::remove_all(path, ec); } } temp{
            std::filesystem::temp_directory_path() / ("CampusLink settings " + stamp)};
        require(std::filesystem::create_directory(temp.path), "could not create isolated settings test folder");
        const auto file = temp.path / "display-v1.conf";
        const auto defaults = load_preferences(file);
        require(!defaults.invalid && defaults.value.language == Language::fr && defaults.value.theme == Theme::light, "incorrect first-run defaults");
        const Preferences alternate{Language::en, Theme::dark, Typeface::mono, 21, true, 960, 600};
        require(save_preferences(file, alternate) && load_preferences(file).value == alternate, "selected appearance not persisted");
        require(save_preferences(file, defaults.value) && load_preferences(file).value == defaults.value, "restore settings did not replace existing file");
        std::ofstream(file) << "invalid\n";
        require(load_preferences(file).invalid && load_preferences(file).value == defaults.value, "corrupt config did not fall back");
        require(!decode(encode(alternate) + "junk"), "accepted trailing settings data");
    });
    check("normal Wi-Fi delegates BSSID selection to Windows without a scope or probes", [] {
        WifiMock b; b.topology.interfaces = {wifi_interface("wifi0", "radio0")}; b.alternate_on_normal = true;
        b.report = net::Reachability::internet;
        const auto result = net::connect_wifi(b, target, {{target}, {}}, net::WifiMode::normal, {});
        require(result.status == net::WifiStatus::internet_reported && b.normal == 1 && b.pinned == 0 && b.probes == 0 &&
            result.link && result.link->bssid != target.bssid, "ordinary connect incorrectly pinned the selected access point");
    });
    check("direct Wi-Fi assistance works without a normal attempt and prefers an independent idle radio on failure", [] {
        WifiMock b; b.topology.interfaces = {wifi_interface("wifi0", "radio0"), wifi_interface("wifi1", "radio1"), wifi_interface("wifi2", "radio0")};
        auto other = target; other.interface_id = "wifi1"; other.bssid.back() = '3'; other.rssi = -70;
        auto same_device = other; same_device.interface_id = "wifi2"; same_device.rssi = -20;
        auto different_ssid = other; different_ssid.ssid = "Another network"; different_ssid.rssi = -25;
        auto unauth = other; unauth.profile.clear(); unauth.rssi = -10;
        b.fail_first = true;
        const auto result = net::connect_wifi(b, target, {{target, same_device, other, different_ssid, unauth}, {}}, net::WifiMode::assisted, {});
        require(result.status == net::WifiStatus::local_link && b.normal == 0 && b.pinned == 2 && b.probes == 0 &&
            b.interfaces == std::vector<std::string>{"wifi0", "wifi1"}, "assistance invoked normal mode or used a non-independent candidate");
    });
    check("assistance preserves a local-only link for sign-in and refuses stale identity and busy radios", [] {
        WifiMock b; b.topology.interfaces = {wifi_interface("wifi0", "radio0"), wifi_interface("wifi1", "radio1")};
        b.topology.interfaces[1].associated = true;
        auto other = target; other.interface_id = "wifi1"; other.bssid.back() = '3';
        auto alternate = target; alternate.bssid.back() = '4';
        const net::Discovery scan{{target, alternate, other}, {}};
        const auto plan = net::wifi_plan(target, scan, b.topology, net::WifiMode::assisted);
        require(plan.size() == 2 && plan[1].bssid == alternate.bssid, "busy radio was picked");
        auto substituted = target; substituted.ssid += "X";
        require(net::connect_wifi(b, substituted, scan, net::WifiMode::assisted, {}).status == net::WifiStatus::invalid && b.pinned == 0, "invalid target mutated host");
        b.report = net::Reachability::local_only;
        const auto result = net::connect_wifi(b, target, scan, net::WifiMode::assisted, {});
        require(result.status == net::WifiStatus::local_link && result.evidence == net::Reachability::local_only && b.pinned == 1,
            "local-only was marked online or disrupted by additional retry");
    });
    check("assistance stops when Windows cannot cancel the previous request", [] {
        WifiMock b; b.topology.interfaces = {wifi_interface("wifi0", "radio0")};
        auto alternative = target; alternative.bssid.back() = '3';
        b.fail_first = b.unsettled = true;
        const auto r = net::connect_wifi(b, target, {{target, alternative}, {}}, net::WifiMode::assisted, {});
        require(r.status == net::WifiStatus::request_unsettled && b.pinned == 1, "retry followed a possibly pending request");
    });
    check("Wi-Fi failures verify rollback and cancellation prevents further attempts", [] {
        WifiMock b;
        auto previous = wifi_interface("wifi0", "radio0"); previous.associated = previous.up = true;
        previous.profile = "Previous"; previous.ssid = "Old"; previous.bssid = "02:00:00:00:00:19";
        previous.address = "192.0.2.102"; previous.gateway = "192.0.2.1";
        b.topology.interfaces = {previous}; b.fail_all = true;
        const auto failed = net::connect_wifi(b, target, {{target}, {}}, net::WifiMode::normal, {});
        require(failed.status == net::WifiStatus::failed && failed.restored && b.pinned == 1 && b.normal == 1, "previous link not verified after rollback");
        b.fail_recovery = true;
        const auto unknown = net::connect_wifi(b, target, {{target}, {}}, net::WifiMode::assisted, {});
        require(unknown.status == net::WifiStatus::recovery_failed && !unknown.restored, "rollback failure hidden");
        auto token = std::make_unique<std::stop_source>(); token->request_stop();
        const auto stopped = net::connect_wifi(b, target, {{target}, {}}, net::WifiMode::assisted, token->get_token());
        require(stopped.status == net::WifiStatus::cancelled && b.pinned == 3, "cancelled run made an attempt");
    });
    check("Wi-Fi budget is clamped, alternatives deduplicated, expired scans do not mutate", [] {
        WifiMock b; b.topology.interfaces = {wifi_interface("wifi0", "radio0")}; b.fail_all = true;
        net::Discovery scan{{target}, {}};
        for (const char suffix : {'3', '4', '5', '6', '7'}) { auto c = target; c.bssid.back() = suffix; scan.bss.push_back(c); scan.bss.push_back(c); }
        const auto now = net::Clock::now();
        const auto expired = net::connect_wifi(b, target, scan, net::WifiMode::assisted, {}, {4, now - 2min});
        require(expired.status == net::WifiStatus::invalid && b.pinned == 0, "expired scan mutated adapter");
        require(net::wifi_plan(target, scan, b.topology, net::WifiMode::assisted, {0, now}, now).size() == 1, "minimum budget invalid");
        const auto hard = net::connect_wifi(b, target, scan, net::WifiMode::assisted, {}, {500, now});
        require(hard.status == net::WifiStatus::failed && hard.attempts.size() == 4 && b.pinned == 4, "attempt bound not respected");
        require(std::adjacent_find(hard.attempts.begin(), hard.attempts.end(), [](const auto& a, const auto& b) {
            return a.candidate.bssid == b.candidate.bssid;
        }) == hard.attempts.end(), "duplicate candidate attempted");
    });
    check("Wi-Fi ranking uses fresh targeted evidence and ignores old or future observations", [] {
        WifiMock b; b.topology.interfaces = {wifi_interface("wifi0", "radio0")};
        auto weak = target, strong = target; weak.bssid.back() = '3'; weak.rssi = -80; strong.bssid.back() = '4'; strong.rssi = -50;
        const net::Discovery scan{{target, weak, strong}, {}};
        const auto now = net::Clock::now();
        std::array history{net::WifiHistory{weak, now - 3min, true}, net::WifiHistory{weak, now + 1min, false}};
        require(net::wifi_plan(target, scan, b.topology, net::WifiMode::assisted, {3, now, history}, now)[1].bssid == weak.bssid, "fresh success not preferred");
        require(net::wifi_plan(target, scan, b.topology, net::WifiMode::assisted, {3, now + 21min, history}, now + 21min)[1].bssid == strong.bssid, "expired evidence retained");
        history[1] = {weak, now - 1min, false};
        require(net::wifi_plan(target, scan, b.topology, net::WifiMode::assisted, {3, now, history}, now)[1].bssid == strong.bssid, "new failure did not suppress older success");
    });
    check("authentication and profile failures stop assistance; radio failures try another access point", [] {
        auto alternative = target; alternative.bssid.back() = '3';
        const net::Discovery scan{{target, alternative}, {}};
        for (const auto cls : {net::WifiFailure::authentication, net::WifiFailure::profile, net::WifiFailure::request}) {
            WifiMock b; b.topology.interfaces = {wifi_interface("wifi0", "radio0")}; b.fail_all = true; b.failure_class = cls;
            const auto r = net::connect_wifi(b, target, scan, net::WifiMode::assisted, {});
            require(r.status == net::WifiStatus::failed && b.pinned == 1 && r.attempts.size() == 1, "network-wide failure tried another BSSID");
        }
        WifiMock b; b.topology.interfaces = {wifi_interface("wifi0", "radio0")}; b.fail_first = true; b.failure_class = net::WifiFailure::radio;
        const auto r = net::connect_wifi(b, target, scan, net::WifiMode::assisted, {});
        require(r.status == net::WifiStatus::local_link && b.pinned == 2 && r.attempts.size() == 2, "radio failure did not try the alternative");
    });
    check("slow DHCP keeps a new association instead of tearing it down or hopping", [] {
        auto alternative = target; alternative.bssid.back() = '3';
        WifiMock b; b.topology.interfaces = {wifi_interface("wifi0", "radio0")}; b.no_address = true;
        const auto r = net::connect_wifi(b, target, {{target, alternative}, {}}, net::WifiMode::assisted, {});
        require(r.status == net::WifiStatus::ip_pending && b.pinned == 1 && r.link && r.link->associated && r.link->bssid == target.bssid,
            "pending address was not kept or another access point was tried");
        // With a usable previous link the user is never left worse off: restore it and stop.
        WifiMock p;
        auto previous = wifi_interface("wifi0", "radio0"); previous.associated = previous.up = true;
        previous.profile = "Previous"; previous.ssid = "Old"; previous.bssid = "02:00:00:00:00:19";
        previous.address = "192.0.2.102"; previous.gateway = "192.0.2.1";
        p.topology.interfaces = {previous}; p.no_address = true;
        const auto restored = net::connect_wifi(p, target, {{target, alternative}, {}}, net::WifiMode::assisted, {});
        require(restored.status == net::WifiStatus::failed && restored.restored && p.pinned == 2 && restored.attempts.size() == 1,
            "address failure did not restore the displaced link or hopped to another BSSID");
    });
    check("Internet evidence is polled until Windows concludes; portals are reported", [] {
        WifiMock b; b.topology.interfaces = {wifi_interface("wifi0", "radio0")};
        b.reports = {net::Reachability::local_only, net::Reachability::local_only, net::Reachability::internet};
        net::WifiPolicy policy; policy.budget.internet = std::chrono::seconds(5);
        const auto online = net::connect_wifi(b, target, {{target}, {}}, net::WifiMode::normal, {}, policy);
        require(online.status == net::WifiStatus::internet_reported && b.reach_calls == 3, "early local-only report was final");
        WifiMock portal; portal.topology.interfaces = {wifi_interface("wifi0", "radio0")};
        portal.reports = {net::Reachability::local_only, net::Reachability::portal};
        require(net::connect_wifi(portal, target, {{target}, {}}, net::WifiMode::normal, {}, policy).status == net::WifiStatus::portal, "portal not reported");
        WifiMock unknown; unknown.topology.interfaces = {wifi_interface("wifi0", "radio0")};
        const auto started = net::Clock::now();
        const auto r = net::connect_wifi(unknown, target, {{target}, {}}, net::WifiMode::normal, {}, policy);
        require(r.status == net::WifiStatus::local_link && unknown.reach_calls == 1 && net::Clock::now() - started < 500ms,
            "waited for evidence the backend cannot provide");
        WifiMock bounded; bounded.topology.interfaces = {wifi_interface("wifi0", "radio0")}; bounded.report = net::Reachability::local_only;
        policy.budget.internet = 1200ms;
        const auto t0 = net::Clock::now();
        require(net::connect_wifi(bounded, target, {{target}, {}}, net::WifiMode::normal, {}, policy).status == net::WifiStatus::local_link &&
            net::Clock::now() - t0 < 3s, "Internet wait not bounded");
    });
    check("a run started on a fresh scan is not invalidated mid-way; progress follows the engine", [] {
        auto alternative = target; alternative.bssid.back() = '3';
        WifiMock b; b.topology.interfaces = {wifi_interface("wifi0", "radio0")}; b.fail_first = true; b.delay = 700ms;
        net::WifiPolicy policy; const auto now = net::Clock::now();
        policy.scanned_at = now - (net::wifi_scan_ttl - 400ms);
        policy.progress = [&](const net::WifiProgress& p) { b.stages.push_back(p.stage); };
        const auto r = net::connect_wifi(b, target, {{target, alternative}, {}}, net::WifiMode::assisted, {}, policy);
        require(r.status == net::WifiStatus::local_link && b.pinned == 2, "scan expiry interrupted an operation in progress");
        using S = net::WifiStage;
        require(b.stages.front() == S::preparing && std::count(b.stages.begin(), b.stages.end(), S::associating) == 2 &&
            b.stages.back() == S::checking_internet &&
            std::find(b.stages.begin(), b.stages.end(), S::obtaining_address) != b.stages.end(), "progress stages incomplete or out of order");
    });
    check("WLAN reason codes map to documented failure classes", [] {
        using F = net::WifiFailure;
        constexpr unsigned ac = 0x20000, msm = 0x30000, sec = 0x40000, half = 0x8000;
        const std::array<std::pair<unsigned, F>, 12> cases{{
            {0, F::unspecified}, {sec + half + 2, F::authentication}, {sec + 5, F::profile}, {0x50000 + 4, F::authentication},
            {0x80000 + 2, F::profile}, {ac + half + 11, F::radio}, {ac + half + 13, F::authentication}, {ac + 2, F::profile},
            {msm + half + 3, F::radio}, {msm + half + 6, F::authentication}, {msm + 4, F::profile}, {0x10001, F::unspecified}}};
        for (const auto& [code, expected] : cases) require(net::classify_wlan_reason(code) == expected, "WLAN reason misclassified");
        require(net::retry_elsewhere(F::radio) && net::retry_elsewhere(F::association_unconfirmed) &&
            !net::retry_elsewhere(F::authentication) && !net::retry_elsewhere(F::ip_unconfirmed), "retry policy inverted");
    });
    check("SSIDs display as UTF-8 and never inject controls", [] {
        require(net::display_ssid("Caf\\xc3\\xa9 Campus") == "Café Campus", "UTF-8 SSID not decoded");
        require(net::display_ssid("Lab\\x5cNet") == "Lab\\Net", "escaped backslash not restored");
        require(net::display_ssid("Bad\\xff") == "Bad\xEF\xBF\xBD" && net::display_ssid("Bell\\x07") == "Bell\xEF\xBF\xBD", "invalid byte or control passed");
        require(net::display_ssid("\\xc0\\xaf") == "\xEF\xBF\xBD\xEF\xBF\xBD", "overlong UTF-8 accepted");
        require(net::display_ssid("Plain-SSID_1") == "Plain-SSID_1" && net::display_ssid("") == "", "plain SSID altered");
    });
    check("read-only path measurement requires approved identity and rejects roaming", [] {
        Mock b;
        const auto scope = net::parse_scope(scope_text);
        b.interface.index = 17;
        require(net::observe_path(b, scope, b.interface.id, {}).state == "SCOPE_BLOCKED" && b.probes == 0, "unauthorized probe");
        b.interface.bssid = target.bssid; b.interface.profile = target.profile;
        auto good_path = net::observe_path(b, scope, b.interface.id, {});
        require(good_path.measured && good_path.measured->host_validated && b.connections == 0 && b.probes == 1, "path not verified without reconnecting");
        b.roam_in_measure = true;
        auto moved = net::observe_path(b, scope, b.interface.id, {});
        require(!moved.measured && moved.state == "VALIDATION_FAILED" && b.connections == 0, "roaming sample attributed to wrong path");
    });
    check("offline schedulers bound reorder window, recover segments and respect shared capacity", [] {
        const auto original = lab::Scenario{{lab::Path{23, 1900, 0, 5}, lab::Path{110, 1450, 0, 20}}, 0, 128};
        for (unsigned scenario = 0; scenario < 3; ++scenario) {
            auto model = original;
            if (scenario == 1) model.shared_kib_per_second = 1500;
            if (scenario == 2) { model.paths[0].loss_every = 7; model.paths[0].outage_start_ms = 220; model.paths[0].outage_end_ms = 1350; }
            for (auto scheduler : {lab::Scheduler::best_single, lab::Scheduler::round_robin, lab::Scheduler::earliest_finish}) {
                const auto result = lab::compare(model, scheduler);
                require(result.valid && result.completed == model.segments && result.peak_buffer <= 15, "invalid model completion/window");
                if (scenario == 1) require(result.mbps <= 1500 * 1024 * 8 / 1e6, "shared model exceeded capacity");
                if (scenario == 2 && scheduler != lab::Scheduler::best_single)
                    require(result.retries && result.delivered[0] && result.delivered[1], "multipath model did not exercise loss and recovery");
                std::cout << "PATH_MODEL," << scenario << ',' << static_cast<unsigned>(scheduler) << ',' <<
                    result.elapsed_ms << ',' << result.mbps << ',' << result.retries << ',' << result.reordered << '\n';
            }
        }
        auto invalid = original; invalid.segments = 4097;
        require(!lab::compare(invalid, lab::Scheduler::round_robin).valid, "invalid scenario accepted");
    });
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
            if (s == Scenario::no_dhcp) require(a.clients().front().discovers == 3 && a.now() >= 400, "DHCP backoff not applied");
            if (s == Scenario::weak_signal) require(a.clients().front().metrics.rssi_dbm < -70, "signal scenario did not affect RSSI");
            if (s == Scenario::expire) require(a.clients().front().leases == 2, "lease renewed");
            if (s == Scenario::slow_dns) require(a.now() >= 300, "DNS delay");
            if (s == Scenario::jitter) require(a.clients().front().metrics.jitter_ms > 10, "jitter not observed");
        });
    }
    check("DHCP renewal retains a slot without restarting DORA", [] {
        const sim::Config config{1, 1, 80, 42, Scenario::renew};
        require(sim::parse(sim::serialize(config)) == config, "renewal scenario cannot be replayed");
        sim::Lab lab(config);
        while (lab.clients().front().state != State::online) lab.step(1);
        const auto before = lab.clients().front();
        require(before.leases == 1 && before.slot == 1, "initial lease missing");
        while (!lab.done()) lab.step(1);
        const auto& client = lab.clients().front();
        require(client.state == State::online && client.leases == 2 && client.discovers == 1 && client.slot == before.slot &&
            client.history[client.history_size - 2].state == State::dhcp_renew && lab.now() < before.expires_at &&
            client.expires_at > before.expires_at, "renewal restarted acquisition or did not extend the existing lease");
    });
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
        auto data = sim::profile({100, 32, 1500, 1000});
        require(data.completed_clients() == 100 && data.end == sim::ProfileEnd::limit_reached, "configured cap was not identified");
        require(data.samples.back().baseline_rss > 0 && data.samples.back().sampled_peak_rss > 0, "missing operating system RSS samples");
        auto small = sim::profile({50000, 1, 1500, 1000});
        require(small.end == sim::ProfileEnd::memory_budget && small.completed_clients() < 50000, "allocation or RSS cap not respected");
        auto token = std::make_unique<std::stop_source>(); token->request_stop();
        require(sim::profile({}, token->get_token()).end == sim::ProfileEnd::cancelled, "capacity cancellation failed");
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
        auto a = net::Candidate{target, good(), net::score(good()), now - 1min, "comparable", {}, true};
        auto b = a; b.bss.bssid.back() = '3'; a.rating.points = 65; b.rating.points = 80;
        require(net::should_switch(a, b, now, now - 2min) && !net::should_switch(a, b, now, now - 5s), "cooldown/hysteresis failed");
        b.observed -= 6min;
        require(net::rank({b, a}, now).front().bss.bssid == a.bss.bssid, "stale candidate won");
    });
    check("handoff ranks only the latest host-validated same-interface evidence", [] {
        const auto now = net::Clock::now();
        const net::Candidate base{target, good(), net::score(good()), now - 1min, "method", {}, true};
        auto a = base; a.rating.points = 50;
        auto b = base; b.bss.bssid.back() = '3';
        auto rejected = b; rejected.observed += 5s; rejected.host_validated = false;
        require(!net::candidate_score(rejected).eligible && !net::should_switch(a, rejected, now, now - 2min), "failed host validation became eligible after reweighting");
        const auto ranked = net::rank({b, rejected, a}, now);
        require(ranked.front().bss.bssid == a.bss.bssid && !ranked[1].rating.eligible && !ranked[2].rating.eligible,
            "older successful sample survived a newer failed attempt");
        b.bss.interface_id = "wifi2";
        require(!net::should_switch(a, b, now, now - 2min), "independent interface treated as a same-interface replacement");
        Mock mock;
        const auto result = net::select(mock, net::parse_scope(scope_text + "require_gateway_reply=true\n"), target, {});
        require(result.rolled_back && result.measured && result.measured->observation.endpoint_verified &&
            !result.measured->host_validated && !result.measured->rating.eligible, "CLP1 success overrode failed gateway policy");
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
    check("distribution and incomplete transfer confidence", [] {
        auto d = net::summarize({80, 4, 2, 6, 8});
        require(d.count == 5 && d.median == 6 && d.p95 == 80 && d.mean == 20, "distribution units/percentiles incorrect");
        require(!net::summarize({3, 2, 1, 0}).p95, "small samples should not imply a percentile");
        net::Observation o;
        o.attempts = 6; o.replies = 5; o.latency_history = {1, 3, 6, 2, 1};
        o.transfers = {{1024, 2, {1, 2}, {2, 3}}, {65536, 2, {15}, {18}}};
        net::summarize_observation(o);
        require(o.endpoint_verified && !o.sample_complete && !net::score(o).eligible && o.down_mbps == 18 && o.stability == 75, "partial series must not rank");
        o.transfers.back().down.push_back(22); o.transfers.back().up.push_back(17);
        net::summarize_observation(o);
        require(o.sample_complete && net::score(o).eligible && o.down_mbps == 20 && o.repeatability == 80, "complete largest-size series must be scored");
    });
    check("experiment waves enforce explicit permission and device independence", [] {
        auto a = net::parse_scope(scope_text), b = a;
        b.bssid.back() = '3'; b.endpoint = "192.0.2.4";
        auto candidate = target; candidate.bssid = b.bssid; candidate.interface_id = "wifi1";
        Mock m; auto i = m.interface; i.hardware_interface = true; i.device_instance = "pci0";
        auto j = i; j.id = "wifi1"; j.device_instance = "pci1";
        const std::vector jobs{net::Experiment{a,target}, net::Experiment{b,candidate}};
        require(net::experiment_waves(jobs, {{i,j}, {}}).size() == 1, "independent adapters not scheduled together");
        j.device_instance = i.device_instance;
        require(net::experiment_waves(jobs, {{i,j}, {}}).size() == 2, "shared device must serialize");
        b.endpoint = a.endpoint;
        j.device_instance = "pci1";
        require(net::experiment_waves({jobs[0], {b,candidate}}, {{i,j}, {}}).size() == 2, "shared endpoint must serialize");
        require(rejected([&] { net::experiment_waves({{a,candidate}}, {{i,j}, {}}); }), "unauthorized batch candidate accepted");
    });
    check("multi-scope codec, hard caps and comparison identity", [] {
        auto second = scope_text;
        second.replace(second.find("00:02"), 5, "00:03");
        const auto list = net::parse_scopes("\xef\xbb\xbf" + scope_text + "sizes=1024,16384,65536\n---\n" + second);
        require(list.size() == 2 && list.front().transfer_sizes().size() == 3 && list.front().method() != list.back().method(), "invalid plan fingerprint");
        require(rejected([] { net::parse_scopes(scope_text + "---\n" + scope_text); }), "duplicate scope accepted");
        require(rejected([] { net::parse_scope(scope_text + "sizes=8192,1024\n"); }), "descending sizes accepted");
        require(rejected([] { net::parse_scope(scope_text + "transfer_repetitions=6\n"); }), "over-budget repetition accepted");
    });
    check("rollback must verify Windows state and gateway policy", [] {
        Mock mock; mock.rollback_wrong_route = true;
        auto scope = net::parse_scope(scope_text + "require_gateway_reply=true\n");
        const auto h = net::select(mock, scope, target, {});
        require(!h.rolled_back && h.state == "ROLLBACK_UNVERIFIED" && mock.connections == 2, "rollback reported verified without a route");
        require(!net::usable_ipv4("169.254.22.3") && !net::usable_ipv4("224.0.0.1") && net::usable_ipv4("192.0.2.1"), "IPv4 validator admitted unusable address");
    });
    check("cancellation restores only the captured link when rollback is allowed", [] {
        for (const bool rollback : {false, true}) {
            Mock mock;
            auto stop = std::make_unique<std::stop_source>(); mock.cancel_in_measure = stop.get();
            auto scope = net::parse_scope(scope_text); scope.rollback = rollback;
            const auto h = net::select(mock, scope, target, stop->get_token());
            require(h.state == (rollback ? "CANCELLED_ROLLED_BACK" : "CANCELLED_HOST_STATE_UNVERIFIED") && h.rolled_back == rollback,
                "cancellation outcome did not report restoration policy");
            require(mock.probes == 1 && mock.connections == (rollback ? 2u : 1u) &&
                mock.interface.profile == (rollback ? "Previous" : "Authorized lab"), "unexpected probe or host mutation during restoration");
        }
    });
    check("50k logical clients deterministic completion", [] {
        sim::Lab lab({50000, 50000, 250, 42, Scenario::healthy});
        while (!lab.done()) lab.step(2048);
        require(lab.events() == 550000 && lab.clients().back().state == State::online, "logical stress transition loss");
    });
    std::cout << "SUMMARY " << passed << " passed, " << failed << " failed\n";
    return static_cast<int>(failed != 0);
}
