#include "campuslink/network.hpp"
#include "campuslink/dns.hpp"
#include <algorithm>
#include <charconv>
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
    if (stop.stop_requested()) return {"CANCELLED", false, std::move(failure), std::move(measured)};
    if (!failure) return {"HOST_VALIDATED", false, {}, std::move(measured)};
    if (changing && scope.rollback && previous.associated && !previous.profile.empty() && usable_ipv4(previous.address)) {
        const Bss rollback{previous.id, previous.ssid, previous.bssid, {}, previous.profile};
        if (const auto rollback_error = backend.connect(rollback, stop)) {
            failure->detail += "; rollback failed: " + rollback_error->operation + " / " + std::to_string(rollback_error->code);
            return {"ROLLBACK_FAILED", false, std::move(failure), std::move(measured)};
        }
        if (!stop.stop_requested()) {
            const auto verified = backend.inspect(stop);
            const auto* restored = find(verified, previous.id);
            if (!stop.stop_requested() && restored && restored->associated && restored->profile == previous.profile &&
                restored->bssid == previous.bssid && usable_ipv4(restored->address) && usable_ipv4(restored->gateway)) {
                return {"ROLLED_BACK", true, std::move(failure), std::move(measured)};
            }
            if (!stop.stop_requested()) {
                failure->detail += "; rollback did not restore association and IPv4 route";
                return {"ROLLBACK_UNVERIFIED", false, std::move(failure), std::move(measured)};
            }
        }
    }
    return {stop.stop_requested() ? "CANCELLED" : "VALIDATION_FAILED", false, std::move(failure), std::move(measured)};
}
}
