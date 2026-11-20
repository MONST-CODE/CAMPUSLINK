#include "campuslink/network.hpp"
#include "campuslink/dns.hpp"
#include <algorithm>
#include <charconv>
#include <thread>
#include <tuple>
#include <stdexcept>

namespace campuslink::net {
namespace {
bool bssid_format(std::string_view text) {
    if (text.size() != 17) return false;
    for (std::size_t i = 0; i < text.size(); ++i) {
        const auto c = text[i];
        if (i % 3 == 2 ? c != ':' : !((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
    }
    return true;
}
bool numeric_ipv4(std::string_view value) {
    const auto original = value;
    unsigned first{};
    for (unsigned i = 0; i < 4; ++i) {
        const auto dot = value.find('.');
        if ((dot == value.npos) != (i == 3)) return false;
        const auto part = value.substr(0, dot);
        unsigned n{};
        const auto [end, err] = std::from_chars(part.data(), part.data() + part.size(), n);
        if (part.empty() || (part.size() > 1 && part.front() == '0') || end != part.data() + part.size() || err != std::errc{} || n > 255) return false;
        if (!i) first = n;
        if (dot != value.npos) value.remove_prefix(dot + 1);
    }
    return first > 0 && first < 224 && first != 127 && !original.ends_with(".255");
}
const Interface* find(const Snapshot& snapshot, const std::string& id) {
    const auto result = std::find_if(snapshot.interfaces.begin(), snapshot.interfaces.end(), [&](const auto& it) { return it.id == id; });
    return result == snapshot.interfaces.end() ? nullptr : &*result;
}
}
bool usable_ipv4(std::string_view addr) { return numeric_ipv4(addr) && !addr.starts_with("169.254."); }
std::vector<unsigned> Scope::transfer_sizes() const { return sizes.empty() ? std::vector<unsigned>{bytes} : sizes; }
std::string Scope::method() const {
    std::string key = endpoint + ':' + std::to_string(port) + " burst=" + std::to_string(transfer_repetitions) +
        " icmp=" + std::to_string(repetitions) + " settle=" + std::to_string(settle_ms) + " timeout=" + std::to_string(timeout_ms);
    key += " dns=" + dns_name + (require_gateway_reply ? " gw=required" : " gw=optional");
    for (const auto n : transfer_sizes()) key += '/' + std::to_string(n);
    return key;
}
bool Scope::valid() const {
    if (sizes.size() > 3 || !std::is_sorted(sizes.begin(), sizes.end()) || std::adjacent_find(sizes.begin(), sizes.end()) != sizes.end()) return false;
    for (const auto size : sizes) if (size < 1024 || size > 65536) return false;
    if (transfer_repetitions < 2 || transfer_repetitions > 5 || settle_ms < 200 || settle_ms > 2000) return false;
    unsigned payload{};
    for (const auto n : transfer_sizes()) payload += n;
    if (payload * 2ull * transfer_repetitions > 1048576) return false;
    return !profile.empty() && profile.size() <= 250 && bssid_format(bssid) && numeric_ipv4(endpoint) && (dns_name.empty() || valid_dns_name(dns_name)) &&
        port >= 1 && port <= 65535 && repetitions >= 3 && repetitions <= 12 && bytes >= 1024 && bytes <= 65536 &&
        timeout_ms >= 200 && timeout_ms <= 2000 && cooldown_s >= 30 && cooldown_s <= 3600;
}
bool Scope::allows(const Bss& candidate) const { return valid() && profile == candidate.profile && bssid == candidate.bssid && !candidate.interface_id.empty(); }
Scope parse_scope(std::string_view text) {
    if (text.size() > 4096 || text.find('\0') != text.npos) throw std::invalid_argument("Invalid scope length or encoding");
    if (text.starts_with("\xef\xbb\xbf")) text.remove_prefix(3);
    Scope s;
    unsigned fields{};
    bool version{};
    auto number = [](std::string_view p) {
        unsigned n{}; const auto result = std::from_chars(p.data(), p.data() + p.size(), n);
        if (p.empty() || result.ec != std::errc{} || result.ptr != p.data() + p.size()) throw std::invalid_argument("Invalid numeric scope field");
        return n;
    };
    while (!text.empty()) {
        const auto end = text.find('\n');
        auto line = text.substr(0, end);
        text.remove_prefix(end == text.npos ? text.size() : end + 1);
        if (line.ends_with('\r')) line.remove_suffix(1);
        if (line.empty() || line.starts_with('#')) continue;
        const auto eq = line.find('=');
        const auto key = line.substr(0, eq);
        if (eq == line.npos) throw std::invalid_argument("Expected a scope key=value record");
        const auto value = line.substr(eq + 1);
        unsigned bit{};
        if (key == "version") { version = value == "1"; bit = 1; }
        else if (key == "profile") { s.profile = value; bit = 2; }
        else if (key == "bssid") { s.bssid = value; bit = 4; }
        else if (key == "endpoint") { s.endpoint = value; bit = 8; }
        else if (key == "port") { s.port = number(value); bit = 16; }
        else if (key == "repetitions") { s.repetitions = number(value); bit = 32; }
        else if (key == "bytes") { s.bytes = number(value); bit = 64; }
        else if (key == "timeout_ms") { s.timeout_ms = number(value); bit = 128; }
        else if (key == "cooldown_s") { s.cooldown_s = number(value); bit = 256; }
        else if (key == "dns_name") { s.dns_name = value; bit = 1024; }
        else if (key == "sizes") {
            bit = 2048;
            auto rest = value;
            for (;;) {
                const auto split = rest.find(',');
                if (s.sizes.size() == 3) throw std::invalid_argument("At most three transfer sizes");
                s.sizes.push_back(number(rest.substr(0, split)));
                if (split == rest.npos) break;
                rest.remove_prefix(split + 1);
            }
        }
        else if (key == "transfer_repetitions") { s.transfer_repetitions = number(value); bit = 4096; }
        else if (key == "settle_ms") { s.settle_ms = number(value); bit = 8192; }
        else if (key == "require_gateway_reply") {
            if (value != "true" && value != "false") throw std::invalid_argument("Invalid gateway policy");
            s.require_gateway_reply = value == "true"; bit = 16384;
        }
        else if (key == "rollback") {
            if (value != "true" && value != "false") throw std::invalid_argument("Invalid rollback policy");
            s.rollback = value == "true"; bit = 512;
        } else throw std::invalid_argument("Unknown scope field");
        if (fields & bit) throw std::invalid_argument("Duplicate scope field");
        fields |= bit;
    }
    if ((fields & 1023) != 1023 || !version || !s.valid()) throw std::invalid_argument("Missing or invalid scope fields");
    return s;
}
std::vector<Scope> parse_scopes(std::string_view text) {
    constexpr auto sep = std::string_view{"\n---\n"};
    if (text.size() > 4096 * 4) throw std::invalid_argument("Scope file too large");
    std::string normalized;
    normalized.reserve(text.size());
    for (const auto c : text) if (c != '\r') normalized += c;
    std::vector<Scope> result;
    text = normalized;
    for (;;) {
        if (result.size() == 4) throw std::invalid_argument("At most four authorized candidates");
        const auto at = text.find(sep);
        auto scope = parse_scope(text.substr(0, at));
        for (const auto& s : result) if (s.profile == scope.profile && s.bssid == scope.bssid) throw std::invalid_argument("Duplicate candidate authorization");
        result.push_back(std::move(scope));
        if (at == text.npos) return result;
        text.remove_prefix(at + sep.size());
    }
}
std::vector<std::vector<std::size_t>> experiment_waves(const std::vector<Experiment>& list, const Snapshot& topology) {
    if (list.empty() || list.size() > 4) throw std::invalid_argument("One to four experiments required");
    std::vector<std::vector<std::size_t>> waves;
    for (std::size_t i = 0; i < list.size(); ++i) {
        const auto& item = list[i];
        if (!item.scope.allows(item.candidate)) throw std::invalid_argument("Experiment is not authorized by its scope");
        const auto* here = find(topology, item.candidate.interface_id);
        if (!waves.empty() && waves.back().size() == 1 && here && here->wireless && here->hardware_interface && !here->device_instance.empty()) {
            const auto& prior = list[waves.back().front()];
            const auto* other = find(topology, prior.candidate.interface_id);
            if (other && other->wireless && other->hardware_interface && !other->device_instance.empty() &&
                other->device_instance != here->device_instance && prior.scope.endpoint != item.scope.endpoint &&
                prior.candidate.interface_id != item.candidate.interface_id) {
                waves.back().push_back(i);
                continue;
            }
        }
        waves.push_back({i});
    }
    return waves;
}
std::vector<Diagnostic> diagnose(const Interface* i, bool detected, const Observation* o) {
    const bool associated = i && i->associated;
    const bool apipa = i && i->address.starts_with("169.254.");
    const bool ip = i && usable_ipv4(i->address);
    return {
        {"Adapter", i ? (i->up ? "PASS" : "WARN") : "UNAVAILABLE", i ? (i->up ? "Interface reports operational" : "Interface not operational") : "No interface selected"},
        {"Discovery", detected ? "PASS" : "UNVERIFIED", detected ? "A BSS was reported" : "No fresh BSS observation"},
        {"Association", i ? (associated ? "PASS" : "WARN") : "UNVERIFIED", associated ? "Windows reports connected" : "Association not confirmed"},
        {"Authentication", associated ? "INFERRED" : "UNVERIFIED", "Association is evidence of access, not a separate authentication trace"},
        {"IPv4 / DHCP", ip ? "PASS" : (associated ? "WARN" : "UNVERIFIED"), apipa ? "APIPA observed; DHCP issue suspected, cause unknown" : (ip ? (i->dhcp ? "IPv4 with DHCP enabled" : "IPv4 present") : "No usable IPv4 read")},
        {"Gateway", o && o->gateway_replied ? "PASS" : (i && i->gateway.empty() && associated ? "WARN" : "UNVERIFIED"), "ICMP may be filtered; gateway route is not proof of reachability"},
        {"DNS", o && o->dns_ms ? "PASS" : (i && i->dns.empty() && associated ? "WARN" : "UNVERIFIED"), "Resolver configuration and successful resolution are separate"},
        {"IP reachability", o && o->replies ? "PASS" : "UNVERIFIED", "ICMP observations concern only the authorized endpoint"},
        {"Application", o && o->endpoint_verified ? "PASS" : "UNVERIFIED", "CLP1 endpoint validation is not general Internet or captive-portal status"}
    };
}
Handoff select(Backend& backend, const Scope& scope, const Bss& candidate, std::stop_token stop) {
    if (stop.stop_requested()) return {"CANCELLED", false, {}, {}};
    if (!scope.allows(candidate)) return {"SCOPE_BLOCKED", false, Error{"handoff", 0, "Candidate does not match authorized profile and BSSID"}, {}};
    auto snapshot = backend.inspect(stop);
    const auto* item = find(snapshot, candidate.interface_id);
    if (!item || !item->wireless) return {"ADAPTER_UNAVAILABLE", false, Error{"handoff", 0, "Selected WLAN interface absent"}, {}};
    const auto previous = *item;
    if (stop.stop_requested()) return {"CANCELLED", false, {}, {}};
    const bool changing = previous.profile != candidate.profile || previous.bssid != candidate.bssid || !previous.associated;
    std::optional<Error> failure;
    if (changing) failure = backend.connect(candidate, stop);
    std::optional<Candidate> measured;
    if (!failure && !stop.stop_requested()) {
        snapshot = backend.inspect(stop);
        item = find(snapshot, candidate.interface_id);
        if (!item || !item->associated || item->profile != candidate.profile || item->bssid != candidate.bssid || !usable_ipv4(item->address) || !usable_ipv4(item->gateway) || (!scope.dns_name.empty() && !usable_ipv4(item->dns))) {
            failure = Error{"validation", 0, "Expected WLAN profile/BSSID, usable IPv4, gateway and DNS configuration not confirmed"};
        } else {
            const auto measurement_start = Clock::now();
            auto observation = backend.measure(*item, scope, stop);
            auto quality = score(observation);
            measured = Candidate{candidate, std::move(observation), quality, Clock::now(), scope.method(), measurement_start};
            if (!measured->observation.endpoint_verified) failure = measured->observation.error.value_or(Error{"validation", 0, "Application endpoint not verified"});
            else if (scope.require_gateway_reply && !measured->observation.gateway_replied) failure = Error{"validation", 0, "Gateway did not answer; required by operator scope"};
            else if (!scope.dns_name.empty() && !measured->observation.dns_ms) failure = Error{"validation", 0, "Configured DNS name was not verified on the WLAN resolver"};
        }
    }
    const bool cancelled = stop.stop_requested();
    if (!failure && !cancelled && measured) {
        measured->host_validated = true;
        return {"HOST_VALIDATED", false, {}, std::move(measured)};
    }
    if (measured) measured->rating.eligible = false;
    if (!failure) failure = Error{"handoff", 0, cancelled ? "Operator cancelled the experiment" : "No validated observation"};
    // Cleanup is a single bounded native connection attempt. Cancellation stops all probes
    // and new trials, but does not interrupt restoration of this interface's captured profile.
    if (failure->request_unsettled) return {"VALIDATION_FAILED", false, std::move(failure), std::move(measured)};
    if (changing && scope.rollback && previous.associated && !previous.profile.empty() && usable_ipv4(previous.address)) {
        const Bss rollback{previous.id, previous.ssid, previous.bssid, {}, previous.profile};
        if (const auto rollback_error = backend.connect(rollback, {})) {
            failure->detail += "; rollback failed: " + rollback_error->operation + " / " + std::to_string(rollback_error->code);
            return {"ROLLBACK_FAILED", false, std::move(failure), std::move(measured)};
        }
        const auto verified = backend.inspect({});
        const auto* restored = find(verified, previous.id);
        if (restored && restored->associated && restored->profile == previous.profile &&
            restored->bssid == previous.bssid && usable_ipv4(restored->address) && usable_ipv4(restored->gateway)) {
            return {cancelled ? "CANCELLED_ROLLED_BACK" : "ROLLED_BACK", true, std::move(failure), std::move(measured)};
        }
        failure->detail += "; rollback did not restore association and IPv4 route";
        return {"ROLLBACK_UNVERIFIED", false, std::move(failure), std::move(measured)};
    }
    if (cancelled && changing) {
        failure->detail += "; no prior usable association with rollback enabled; host state not restored";
        return {"CANCELLED_HOST_STATE_UNVERIFIED", false, std::move(failure), std::move(measured)};
    }
    return {cancelled ? "CANCELLED" : "VALIDATION_FAILED", false, std::move(failure), std::move(measured)};
}
Handoff observe_path(Backend& backend, const Scope& scope, std::string_view interface_id, std::stop_token stop) {
    if (stop.stop_requested()) return {"CANCELLED", false, {}, {}};
    const auto before = backend.inspect(stop);
    const auto* i = find(before, std::string(interface_id));
    if (stop.stop_requested()) return {"CANCELLED", false, {}, {}};
    if (!i || !i->wireless || !i->associated || !i->up || !i->index || !usable_ipv4(i->address) || !usable_ipv4(i->gateway))
        return {"ADAPTER_UNAVAILABLE", false, Error{"validation", 0, "Expected Wi-Fi link and usable IPv4 gateway not confirmed"}, {}};
    const Bss b{i->id, i->ssid, i->bssid, {}, i->profile};
    if (!scope.allows(b)) return {"SCOPE_BLOCKED", false, Error{"handoff", 0, "Candidate does not match authorized profile and BSSID"}, {}};
    const auto started = Clock::now();
    auto data = backend.measure(*i, scope, stop);
    if (stop.stop_requested()) return {"CANCELLED", false, {}, {}};
    const auto after = backend.inspect(stop);
    if (stop.stop_requested()) return {"CANCELLED", false, {}, {}};
    const auto* latest = find(after, i->id);
    if (!latest || !latest->associated || !latest->up || latest->index != i->index || latest->address != i->address ||
        latest->gateway != i->gateway || latest->ssid != i->ssid || latest->profile != i->profile || latest->bssid != i->bssid ||
        (!scope.dns_name.empty() && latest->dns != i->dns))
        return {"VALIDATION_FAILED", false, Error{"validation", 0, "Observed path changed during the measurement"}, {}};
    Candidate measured{b, std::move(data), {}, Clock::now(), scope.method(), started};
    measured.host_validated = measured.observation.endpoint_verified && (!scope.require_gateway_reply || measured.observation.gateway_replied) &&
        (scope.dns_name.empty() || measured.observation.dns_ms.has_value());
    measured.rating = candidate_score(measured, base_weights);
    auto error = measured.host_validated ? std::nullopt : measured.observation.error;
    return {measured.host_validated ? "HOST_VALIDATED" : "VALIDATION_FAILED", false, std::move(error), std::move(measured)};
}
namespace {
bool same_bss(const Bss& a, const Bss& b) {
    return a.interface_id == b.interface_id && a.bssid == b.bssid && a.ssid == b.ssid && a.profile == b.profile &&
        a.security == b.security && a.infrastructure && b.infrastructure;
}
bool usable_link(const Interface& i) {
    return i.wireless && i.up && i.associated && usable_ipv4(i.address) && usable_ipv4(i.gateway);
}
bool matches_wifi(const Interface& i, const Bss& b, bool exact) {
    return i.associated && i.profile == b.profile && i.ssid == b.ssid && (!exact || i.bssid == b.bssid);
}
bool fresh_wifi(const WifiPolicy& policy, Clock::time_point now) {
    return policy.scanned_at == Clock::time_point{} || (policy.scanned_at <= now && now - policy.scanned_at < wifi_scan_ttl);
}
int history_tier(const Bss& b, const WifiPolicy& policy, Clock::time_point now) {
    const WifiHistory* latest{};
    for (const auto& h : policy.history) {
        if (h.observed > now || !same_bss(h.candidate, b) || (latest && latest->observed >= h.observed)) continue;
        latest = &h;
    }
    if (!latest) return 0;
    const auto age = now - latest->observed;
    // Only short-lived evidence can override signal. Never extrapolate quality from it.
    if (latest->success && age < std::chrono::minutes(20)) return 1;
    if (!latest->success && age < std::chrono::minutes(5)) return -1;
    return 0;
}
// Cooperative wait. Returns false when cancelled.
bool wait_for(std::chrono::milliseconds span, std::stop_token stop) {
    const auto until = Clock::now() + span;
    while (Clock::now() < until) {
        if (stop.stop_requested()) return false;
        std::this_thread::sleep_for(std::min<Clock::duration>(std::chrono::milliseconds(100), until - Clock::now()));
    }
    return !stop.stop_requested();
}
// Windows' NCSI probe completes some seconds after the address. A single early read reports
// "local only" on working networks, so poll while the report is local-only. "unknown" means the
// backend cannot tell (no Network List Manager): do not wait for something that will not come.
Reachability settle_reachability(Backend& backend, const Interface& link, std::chrono::milliseconds budget, std::stop_token stop) {
    const auto deadline = Clock::now() + budget;
    auto evidence = backend.reachability(link);
    while (evidence == Reachability::local_only && Clock::now() < deadline) {
        if (!wait_for(std::chrono::milliseconds(750), stop)) break;
        evidence = backend.reachability(link);
    }
    return evidence;
}
}
WifiFailure classify_wlan_reason(unsigned reason) {
    // Documented L2 reason groups (l2cmn.h / wlanapi.h). Each group is 0x10000 wide;
    // the upper half of a group holds connection-time codes.
    constexpr unsigned group = 0x10000, half = group / 2;
    constexpr unsigned ac = 0x20000, msm = 0x30000, security = 0x40000, onex = 0x50000, profile = 0x80000;
    if (reason == 0) return WifiFailure::unspecified;
    if (reason >= onex && reason < onex + group) return WifiFailure::authentication;
    if (reason >= security && reason < security + group) return reason < security + half ? WifiFailure::profile : WifiFailure::authentication;
    if (reason >= profile && reason < profile + group) return WifiFailure::profile;
    if (reason >= ac && reason < ac + group) {
        switch (reason - ac) {
        case 1: case 2: return WifiFailure::profile;                           // network / profile not compatible
        case half + 2: case half + 6: case half + 9: case half + 10: case half + 11:
            return WifiFailure::radio;                                         // not visible, failed list, connect/scan call, not available
        case half + 13: return WifiFailure::authentication;                    // key mismatch
        case half + 3: case half + 4: case half + 7: case half + 12: case half + 15: case half + 16: case half + 17:
            return WifiFailure::profile;                                       // policy denied, blocked, profile changed, not allowed
        default: return WifiFailure::unspecified;
        }
    }
    if (reason >= msm && reason < msm + group) {
        const auto code = reason - msm;
        if (code >= 1 && code <= 5) return WifiFailure::profile;               // security/phy/rate set not supported
        switch (code) {
        case half + 2: case half + 3: case half + 8: case half + 11: case half + 12: return WifiFailure::radio;
        case half + 4: case half + 5: case half + 6: case half + 7: case half + 9: case half + 18: return WifiFailure::authentication;
        default: return WifiFailure::unspecified;
        }
    }
    return WifiFailure::unspecified;
}
std::string display_ssid(std::string_view escaped) {
    std::string bytes;
    bytes.reserve(escaped.size());
    for (std::size_t i = 0; i < escaped.size(); ++i) {
        unsigned value{};
        if (escaped[i] == '\\' && i + 3 < escaped.size() && escaped[i + 1] == 'x' &&
            std::from_chars(escaped.data() + i + 2, escaped.data() + i + 4, value, 16).ptr == escaped.data() + i + 4) {
            bytes += static_cast<char>(value); i += 3;
        } else bytes += escaped[i];
    }
    // Keep only well-formed UTF-8 (no overlongs, surrogates or C0/DEL controls); anything else
    // is shown as U+FFFD so the label can never break the layout or impersonate other text.
    std::string out;
    const auto* p = reinterpret_cast<const unsigned char*>(bytes.data());
    const auto* end = p + bytes.size();
    while (p < end) {
        const unsigned c = *p;
        unsigned length = c < 0x80 ? 1 : (c >> 5) == 0x6 ? 2 : (c >> 4) == 0xE ? 3 : (c >> 3) == 0x1E ? 4 : 0;
        bool valid = length && static_cast<std::size_t>(end - p) >= length;
        unsigned code = length == 1 ? c : length == 2 ? c & 0x1F : length == 3 ? c & 0x0F : c & 0x07;
        for (unsigned k = 1; valid && k < length; ++k) {
            valid = (p[k] & 0xC0) == 0x80;
            code = (code << 6) | (p[k] & 0x3F);
        }
        if (valid) valid = !(length == 2 && code < 0x80) && !(length == 3 && (code < 0x800 || (code >= 0xD800 && code <= 0xDFFF))) &&
            !(length == 4 && (code < 0x10000 || code > 0x10FFFF)) && !(code < 0x20 || code == 0x7F);
        if (valid) { out.append(reinterpret_cast<const char*>(p), length); p += length; }
        else { out += "\xEF\xBF\xBD"; ++p; }
    }
    return out;
}
std::vector<Bss> wifi_plan(const Bss& selected, const Discovery& scan, const Snapshot& topology, WifiMode mode, WifiPolicy policy, Clock::time_point now) {
    if (!fresh_wifi(policy, now) || selected.profile.empty() || selected.ssid.empty() || !bssid_format(selected.bssid) ||
        !std::any_of(scan.bss.begin(), scan.bss.end(), [&](const auto& b) { return same_bss(selected, b); })) return {};
    const auto* primary = find(topology, selected.interface_id);
    if (!primary || !primary->wireless) return {};
    if (mode == WifiMode::normal) return {selected};
    struct Choice { const Bss* b; bool independent; int history; };
    std::vector<Choice> alternatives;
    for (const auto& b : scan.bss) {
        if (b.profile != selected.profile || b.ssid != selected.ssid || b.security != selected.security || !b.infrastructure ||
            !bssid_format(b.bssid) || same_bss(b, selected)) continue;
        const auto* adapter = find(topology, b.interface_id);
        if (!adapter || !adapter->wireless) continue;
        const bool independent = adapter->id != primary->id;
        if (independent && (!adapter->hardware_interface || !primary->hardware_interface || !adapter->wlan_idle || adapter->associated ||
            adapter->device_instance.empty() || primary->device_instance.empty() || adapter->device_instance == primary->device_instance)) continue;
        alternatives.push_back({&b, independent, history_tier(b, policy, now)});
    }
    std::sort(alternatives.begin(), alternatives.end(), [](const Choice& a, const Choice& b) {
        if (a.history != b.history) return a.history > b.history;
        if (a.independent != b.independent) return a.independent;
        if (a.b->rssi != b.b->rssi) return a.b->rssi > b.b->rssi;
        return std::tie(a.b->interface_id, a.b->bssid) < std::tie(b.b->interface_id, b.b->bssid);
    });
    std::vector<Bss> plan{selected};
    for (const auto& c : alternatives) {
        if (plan.size() >= std::clamp(policy.max_attempts, 1u, wifi_max_attempts)) break;
        if (std::none_of(plan.begin(), plan.end(), [&](const auto& p) { return same_bss(p, *c.b); })) plan.push_back(*c.b);
    }
    return plan;
}
WifiResult connect_wifi(Backend& backend, const Bss& selected, const Discovery& scan, WifiMode mode, std::stop_token stop, WifiPolicy policy) {
    const auto start = Clock::now();
    WifiResult result;
    const auto finish = [&](WifiStatus status) {
        result.finished = Clock::now();
        result.elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(result.finished - start);
        result.status = status;
        return std::move(result);
    };
    std::size_t total{};
    const auto report = [&](WifiStage stage, std::size_t index, const std::string& bssid) {
        if (policy.progress) policy.progress({stage, static_cast<unsigned>(index), static_cast<unsigned>(total), bssid});
    };
    if (stop.stop_requested()) return finish(WifiStatus::cancelled);
    // Freshness gates the decision to start. Once started, an attempt in progress is never
    // invalidated by the clock: the plan was built from that scan and is bounded.
    if (!fresh_wifi(policy, start)) return finish(WifiStatus::invalid);
    report(WifiStage::preparing, 0, selected.bssid);
    const auto topology = backend.inspect(stop);
    const auto plan = wifi_plan(selected, scan, topology, mode, policy, start);
    total = plan.size();
    if (stop.stop_requested()) return finish(WifiStatus::cancelled);
    if (plan.empty()) return finish(WifiStatus::invalid);
    // An operation may restore at most one displaced link. If a later mutation
    // would require another restoration, skip it rather than overspend the budget.
    const bool exact = mode == WifiMode::assisted;
    for (std::size_t index = 0; index < plan.size(); ++index) {
        const auto& trial = plan[index];
        if (stop.stop_requested()) return finish(WifiStatus::cancelled);
        const auto before = backend.inspect(stop);
        const auto* previous = find(before, trial.interface_id);
        if (stop.stop_requested()) return finish(WifiStatus::cancelled);
        if (!previous || !previous->wireless) continue;
        if (trial.interface_id != selected.interface_id) {
            const auto* primary = find(before, selected.interface_id);
            if (previous->associated || !previous->wlan_idle || !primary || !primary->hardware_interface || !previous->hardware_interface ||
                previous->device_instance.empty() || primary->device_instance.empty() || previous->device_instance == primary->device_instance) continue;
        }
        const bool mutation = !(usable_link(*previous) && matches_wifi(*previous, trial, exact));
        if (mutation && previous->associated && result.recovery_attempted) continue;
        const auto tier = history_tier(trial, policy, start);
        const auto reason = same_bss(trial, selected) ? WifiReason::selected : tier > 0 ? WifiReason::previous_success :
            tier < 0 ? WifiReason::avoid_recent_failure : trial.interface_id != selected.interface_id ? WifiReason::independent_idle : WifiReason::signal;
        WifiAttempt attempt{trial, {}, {}, reason};
        const auto attempted_at = Clock::now();
        report(WifiStage::associating, index + 1, trial.bssid);
        const WifiProgressSink forward = policy.progress ? WifiProgressSink([&](const WifiProgress& p) {
            report(p.stage, index + 1, trial.bssid);
        }) : WifiProgressSink{};
        if (mutation) attempt.error = backend.request_wifi(trial, mode, stop, attempt.timing, policy.budget, forward);
        auto& failure = attempt.error;
        const auto after = backend.inspect(stop);
        const auto* linked = find(after, trial.interface_id);
        if (!failure && !stop.stop_requested() && linked && usable_link(*linked) && matches_wifi(*linked, trial, exact)) {
            report(WifiStage::checking_internet, index + 1, trial.bssid);
            result.evidence = settle_reachability(backend, *linked, policy.budget.internet, stop);
            if (result.evidence == Reachability::internet)
                attempt.timing.internet_reported = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - attempted_at);
            result.attempts.push_back(std::move(attempt));
            result.link = *linked;
            // Keep a local-only or portal link for Windows sign-in. Never try other credentials.
            return finish(result.evidence == Reachability::internet ? WifiStatus::internet_reported :
                          result.evidence == Reachability::portal ? WifiStatus::portal : WifiStatus::local_link);
        }
        if (!failure && !stop.stop_requested()) failure = Error{"validation", 0, "Expected Wi-Fi link and usable IPv4 gateway not confirmed", false,
            linked && matches_wifi(*linked, trial, exact) ? WifiFailure::ip_unconfirmed : WifiFailure::association_unconfirmed};
        if (stop.stop_requested() && !failure) failure = Error{"WlanConnect", 1223, "Cancelled before connection"};
        const bool unsettled = failure && failure->request_unsettled;
        const auto failure_class = failure ? failure->wifi_failure : WifiFailure::unspecified;
        // Associated but the address is still being negotiated. If nothing usable was displaced,
        // keep the association: Windows continues DHCP and tearing it down can only make it worse.
        const bool displaced_usable = mutation && usable_link(*previous) && bssid_format(previous->bssid) && !previous->profile.empty();
        if (failure_class == WifiFailure::ip_unconfirmed && !displaced_usable && !stop.stop_requested() && linked && matches_wifi(*linked, trial, exact)) {
            result.attempts.push_back(std::move(attempt));
            result.link = *linked;
            return finish(WifiStatus::ip_pending);
        }
        result.attempts.push_back(std::move(attempt));
        if (unsettled) return finish(WifiStatus::request_unsettled);
        if (displaced_usable) {
            const Bss restore{previous->id, previous->ssid, previous->bssid, {}, previous->profile};
            auto recovery = backend.inspect({});
            const auto recovered = [&] {
                const auto* old = find(recovery, previous->id);
                return old && usable_link(*old) && matches_wifi(*old, restore, true);
            };
            if (!recovered()) {
                const auto* current = find(recovery, previous->id);
                if (current && current->associated && !matches_wifi(*current, restore, true) && !matches_wifi(*current, trial, exact))
                    return finish(WifiStatus::recovery_failed); // Do not override an observed external change.
                result.recovery_attempted = true;
                report(WifiStage::restoring, index + 1, previous->bssid);
                const auto error = backend.connect(restore, {});
                recovery = backend.inspect({});
                if (error || !recovered()) return finish(WifiStatus::recovery_failed);
            }
            result.restored = true;
        }
        if (stop.stop_requested()) return finish(WifiStatus::cancelled);
        // Only radio/association problems justify another access point of the same network.
        // Authentication, profile and address problems are network-wide: stop and explain.
        if (!retry_elsewhere(failure_class)) break;
    }
    return finish(WifiStatus::failed);
}

}
