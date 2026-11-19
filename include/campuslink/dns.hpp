#pragma once
#include <array>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>
namespace campuslink::net {
bool valid_dns_name(std::string_view);
std::vector<std::uint8_t> dns_query(std::string_view, std::uint16_t id);
bool dns_confirms(std::span<const std::uint8_t> question, std::span<const std::uint8_t> reply,
    std::array<std::uint8_t,4> address);
}
