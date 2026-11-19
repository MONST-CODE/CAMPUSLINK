#include "campuslink/network.hpp"
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
bool Scope::valid() const {
    return !profile.empty() && profile.size() <= 250 && bssid_format(bssid) && numeric_ipv4(endpoint) &&
        port >= 1 && port <= 65535 && repetitions >= 3 && repetitions <= 8 && bytes >= 1024 && bytes <= 65536 &&
        timeout_ms >= 200 && timeout_ms <= 2000 && cooldown_s >= 30 && cooldown_s <= 3600;
}
bool Scope::allows(const Bss& candidate) const { return valid() && profile == candidate.profile && bssid == candidate.bssid && !candidate.interface_id.empty(); }
Scope parse_scope(std::string_view text) {
    if (text.size() > 4096 || text.find('\0') != text.npos) throw std::invalid_argument("Invalid scope length or encoding");
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
        else if (key == "rollback") {
            if (value != "true" && value != "false") throw std::invalid_argument("Invalid rollback policy");
            s.rollback = value == "true"; bit = 512;
        } else throw std::invalid_argument("Unknown scope field");
        if (fields & bit) throw std::invalid_argument("Duplicate scope field");
        fields |= bit;
    }
    if (fields != 1023 || !version || !s.valid()) throw std::invalid_argument("Missing or invalid scope fields");
    return s;
}
std::vector<Diagnostic> diagnose(const Interface* i, bool detected, const Observation* o) {
    const bool associated = i && i->associated;
    const bool apipa = i && i->address.starts_with("169.254.");
    const bool ip = i && !i->address.empty() && !apipa;
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
        if (!item || !item->associated || item->profile != candidate.profile || item->bssid != candidate.bssid || item->address.empty() ||
            item->address.starts_with("169.254.") || item->gateway.empty() || item->dns.empty()) {
            failure = Error{"validation", 0, "Expected WLAN profile/BSSID, usable IPv4, gateway and DNS configuration not confirmed"};
        } else {
            auto observation = backend.measure(*item, scope, stop);
            auto quality = score(observation);
            measured = Candidate{candidate, std::move(observation), quality, Clock::now()};
            if (!measured->observation.endpoint_verified) failure = measured->observation.error.value_or(Error{"validation", 0, "Application endpoint not verified"});
        }
    }
    if (stop.stop_requested()) return {"CANCELLED", false, std::move(failure), std::move(measured)};
    if (!failure) return {"HOST_VALIDATED", false, {}, std::move(measured)};
    if (changing && scope.rollback && previous.associated && !previous.profile.empty() && !previous.address.empty() && !previous.address.starts_with("169.254.")) {
        const Bss rollback{previous.id, previous.ssid, previous.bssid, {}, previous.profile};
        if (const auto rollback_error = backend.connect(rollback, stop)) {
            failure->detail += "; rollback failed: " + rollback_error->operation + " / " + std::to_string(rollback_error->code);
            return {"ROLLBACK_FAILED", false, std::move(failure), std::move(measured)};
        }
        if (!stop.stop_requested()) return {"ROLLED_BACK", true, std::move(failure), std::move(measured)};
    }
    return {stop.stop_requested() ? "CANCELLED" : "VALIDATION_FAILED", false, std::move(failure), std::move(measured)};
}
}
