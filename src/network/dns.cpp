#include "campuslink/dns.hpp"
#include <algorithm>
#include <stdexcept>

namespace campuslink::net {
bool valid_dns_name(std::string_view name) {
    if (name.size() < 3 || name.size() > 253 || name.find('.') == name.npos) return false;
    for (;;) {
        const auto at = name.find('.');
        const auto label = name.substr(0, at);
        if (label.empty() || label.size() > 63 || label.front() == '-' || label.back() == '-') return false;
        for (const char c : label) if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-')) return false;
        if (at == name.npos) return true;
        name.remove_prefix(at + 1);
    }
}
std::vector<std::uint8_t> dns_query(std::string_view name, std::uint16_t id) {
    if (!valid_dns_name(name)) throw std::invalid_argument("Invalid controlled DNS query name");
    std::vector<std::uint8_t> out{static_cast<std::uint8_t>(id >> 8), static_cast<std::uint8_t>(id), 1, 0, 0, 1, 0, 0, 0, 0, 0, 0};
    for (;;) {
        const auto dot = name.find('.');
        const auto label = name.substr(0, dot);
        out.push_back(static_cast<std::uint8_t>(label.size()));
        for (const auto c : label) out.push_back(static_cast<std::uint8_t>(c));
        if (dot == name.npos) break;
        name.remove_prefix(dot + 1);
    }
    const std::array<std::uint8_t,5> tail{0,0,1,0,1};
    out.insert(out.end(), tail.begin(), tail.end());
    return out;
}
bool dns_confirms(std::span<const std::uint8_t> question, std::span<const std::uint8_t> reply, std::array<std::uint8_t,4> address) {
    if (question.size() < 17 || reply.size() < question.size() || reply.size() > 512 || reply[0] != question[0] || reply[1] != question[1] ||
        (reply[2] & 0xfa) != 0x80 || (reply[3] & 0x0f) != 0 || reply[4] != 0 || reply[5] != 1 ||
        !std::equal(question.begin() + 12, question.end(), reply.begin() + 12)) return false;
    const auto count = (static_cast<unsigned>(reply[6]) << 8) | reply[7];
    auto pos = question.size();
    for (unsigned i = 0; i < count; ++i) {
        // A match is accepted only for an A RR whose owner equals the original question name.
        const auto begin = pos;
        bool match = false;
        for (;;) {
            if (pos >= reply.size()) return false;
            const auto length = reply[pos++];
            if (!length) {
                const auto size = question.size() - 16;
                match = pos - begin == size && std::equal(reply.begin() + begin, reply.begin() + pos, question.begin() + 12);
                break;
            }
            if ((length & 0xc0) == 0xc0) {
                if (pos >= reply.size()) return false;
                match = begin + 1 == pos && length == 0xc0 && reply[pos] == 12;
                ++pos; break;
            }
            if (length > 63 || length > reply.size() - pos) return false;
            pos += length;
        }
        if (reply.size() - pos < 10) return false;
        const auto type = (static_cast<unsigned>(reply[pos]) << 8) | reply[pos + 1];
        const auto rrclass = (static_cast<unsigned>(reply[pos + 2]) << 8) | reply[pos + 3];
        const auto bytes = (static_cast<unsigned>(reply[pos + 8]) << 8) | reply[pos + 9];
        pos += 10;
        if (bytes > reply.size() - pos) return false;
        if (match && type == 1 && rrclass == 1 && bytes == 4 && std::equal(address.begin(), address.end(), reply.begin() + pos)) return true;
        pos += bytes;
    }
    return false;
}
}
