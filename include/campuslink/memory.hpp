#pragma once
// Per-network memory store (ADR 0004). Pure, header-only, no SDL or Win32, so it is unit-tested
// directly. The engine consumes a single NetworkRecord (see network.hpp); this file only stores,
// looks up and updates records, and serializes them to the user data folder.
//
// Format (versioned, bounded, tab-separated so an escaped SSID — which never contains a tab or a
// control byte — cannot break a field):
//   CAMPUSLINK_NET_V1\n
//   ssid \t security \t best_bssid \t successes \t failures \t portal \t assoc_ms \t dhcp_ms \t last_seen \n
// assoc_ms / dhcp_ms are -1 when unknown. Nothing here contains credentials.
#include "campuslink/network.hpp"
#include <algorithm>
#include <array>
#include <charconv>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace campuslink::net {
inline constexpr std::size_t network_memory_cap = 256;       // keep the most recent networks
inline constexpr std::size_t network_memory_bytes = 128 * 1024;

inline bool same_network(const NetworkRecord& r, std::string_view ssid, std::string_view security) {
    return r.ssid == ssid && r.security == security;
}
inline const NetworkRecord* find_record(const std::vector<NetworkRecord>& store, std::string_view ssid, std::string_view security) {
    const auto it = std::find_if(store.begin(), store.end(), [&](const auto& r) { return same_network(r, ssid, security); });
    return it == store.end() ? nullptr : &*it;
}
inline NetworkRecord& upsert_record(std::vector<NetworkRecord>& store, std::string_view ssid, std::string_view security) {
    for (auto& r : store) if (same_network(r, ssid, security)) return r;
    store.push_back({std::string(ssid), std::string(security), {}, 0, 0, false, {}, {}, 0});
    return store.back();
}
inline void prune_records(std::vector<NetworkRecord>& store, std::size_t cap = network_memory_cap) {
    if (store.size() <= cap) return;
    std::sort(store.begin(), store.end(), [](const auto& a, const auto& b) { return a.last_seen > b.last_seen; });
    store.resize(cap);
}
// Smooth a timing so one slow sample does not dominate: weight the previous value 3:1.
inline std::optional<unsigned> smooth(std::optional<unsigned> previous, std::optional<unsigned> sample) {
    if (!sample) return previous;
    if (!previous) return sample;
    return static_cast<unsigned>((static_cast<std::uint64_t>(*previous) * 3 + *sample) / 4);
}
// Record one connection outcome against the network's record. `best_bssid` is the access point that
// reached a usable link (empty on failure). Timings are kept only on success.
inline void record_outcome(std::vector<NetworkRecord>& store, std::string_view ssid, std::string_view security,
                           bool success, bool portal, std::string_view best_bssid,
                           std::optional<unsigned> assoc_ms, std::optional<unsigned> dhcp_ms, long long now_epoch) {
    auto& r = upsert_record(store, ssid, security);
    r.last_seen = now_epoch;
    if (portal) r.portal_seen = true;
    if (success) {
        if (r.successes < 0xffffffu) ++r.successes;
        if (!best_bssid.empty()) r.best_bssid = std::string(best_bssid);
        r.assoc_ms = smooth(r.assoc_ms, assoc_ms);
        r.dhcp_ms = smooth(r.dhcp_ms, dhcp_ms);
    } else if (r.failures < 0xffffffu) {
        ++r.failures;
    }
    prune_records(store);
}
inline std::string encode_networks(std::span<const NetworkRecord> store) {
    std::string out = "CAMPUSLINK_NET_V1\n";
    const auto num = [](std::optional<unsigned> v) { return v ? std::to_string(*v) : std::string("-1"); };
    for (const auto& r : store) {
        if (r.ssid.empty()) continue; // never key on a hidden/empty SSID
        out += r.ssid; out += '\t'; out += r.security; out += '\t'; out += r.best_bssid; out += '\t';
        out += std::to_string(r.successes); out += '\t'; out += std::to_string(r.failures); out += '\t';
        out += (r.portal_seen ? '1' : '0'); out += '\t'; out += num(r.assoc_ms); out += '\t'; out += num(r.dhcp_ms); out += '\t';
        out += std::to_string(r.last_seen); out += '\n';
    }
    return out;
}
inline std::optional<std::vector<NetworkRecord>> decode_networks(std::string_view text) {
    if (text.size() > network_memory_bytes) return std::nullopt;
    const auto line = [&]() -> std::optional<std::string_view> {
        if (text.empty()) return std::nullopt;
        const auto nl = text.find('\n');
        auto row = text.substr(0, nl == text.npos ? text.size() : nl);
        text.remove_prefix(nl == text.npos ? text.size() : nl + 1);
        return row;
    };
    const auto header = line();
    if (!header || *header != "CAMPUSLINK_NET_V1") return std::nullopt;
    std::vector<NetworkRecord> store;
    for (auto row = line(); row; row = line()) {
        if (row->empty()) continue;
        std::array<std::string_view, 9> f{};
        std::string_view rest = *row;
        for (int i = 0; i < 9; ++i) {
            const auto tab = rest.find('\t');
            if (i < 8 ? tab == rest.npos : tab != rest.npos) return std::nullopt; // exactly 9 fields
            f[i] = rest.substr(0, i < 8 ? tab : rest.size());
            if (i < 8) rest.remove_prefix(tab + 1);
        }
        NetworkRecord r;
        r.ssid = std::string(f[0]); r.security = std::string(f[1]); r.best_bssid = std::string(f[2]);
        const auto u = [](std::string_view s, unsigned& v) {
            const auto [p, e] = std::from_chars(s.data(), s.data() + s.size(), v);
            return e == std::errc{} && p == s.data() + s.size();
        };
        const auto opt = [&](std::string_view s, std::optional<unsigned>& v) {
            if (s == "-1") { v = std::nullopt; return true; }
            unsigned n{}; if (!u(s, n)) return false; v = n; return true;
        };
        unsigned portal{};
        if (r.ssid.empty() || f[1].find('\t') != f[1].npos || !u(f[3], r.successes) || !u(f[4], r.failures) ||
            !u(f[5], portal) || portal > 1 || !opt(f[6], r.assoc_ms) || !opt(f[7], r.dhcp_ms)) return std::nullopt;
        r.portal_seen = portal == 1;
        const auto [p, e] = std::from_chars(f[8].data(), f[8].data() + f[8].size(), r.last_seen);
        if (e != std::errc{} || p != f[8].data() + f[8].size()) return std::nullopt;
        if (store.size() >= network_memory_cap) return std::nullopt;
        store.push_back(std::move(r));
    }
    return store;
}
}
