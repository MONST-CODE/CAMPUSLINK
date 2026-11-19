#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <iphlpapi.h>
#include <icmpapi.h>
#include <wlanapi.h>
#include <setupapi.h>
#include <objbase.h>

#include "campuslink/network.hpp"
#include "campuslink/dns.hpp"
#include <algorithm>
#include <atomic>
#include <charconv>
#include <cstddef>
#include <cmath>
#include <cstdio>
#include <memory>
#include <numeric>
#include <thread>

namespace campuslink::net {
namespace {
using namespace std::chrono_literals;
std::string utf8(const wchar_t* wide) {
    const int count = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide, -1, nullptr, 0, nullptr, nullptr);
    if (count <= 1) return {};
    std::string text(static_cast<std::size_t>(count), '\0');
    if (!WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide, -1, text.data(), count, nullptr, nullptr)) return {};
    text.pop_back(); return text;
}
std::string guid_text(const GUID& id) {
    wchar_t buffer[40]{};
    if (!StringFromGUID2(id, buffer, 40)) return {};
    return utf8(buffer);
}
bool equal_id(std::string a, std::string b) {
    auto normalize = [](std::string& s) { for (auto& c : s) if (c >= 'A' && c <= 'F') c += 'a' - 'A'; };
    normalize(a); normalize(b); return a == b;
}
std::string label(const DOT11_SSID& s) {
    std::string text;
    for (ULONG i = 0; i < (std::min)(s.uSSIDLength, static_cast<ULONG>(DOT11_SSID_MAX_LENGTH)); ++i) {
        const auto c = s.ucSSID[i];
        if (c >= 32 && c <= 126 && c != '\\') text += static_cast<char>(c);
        else { char encoded[5]{}; std::snprintf(encoded, sizeof encoded, "\\x%02x", static_cast<unsigned>(c)); text += encoded; }
    }
    return text;
}
std::string mac(const DOT11_MAC_ADDRESS& a) {
    char text[18]{};
    std::snprintf(text, sizeof text, "%02x:%02x:%02x:%02x:%02x:%02x", static_cast<unsigned>(a[0]),
        static_cast<unsigned>(a[1]), static_cast<unsigned>(a[2]), static_cast<unsigned>(a[3]), static_cast<unsigned>(a[4]), static_cast<unsigned>(a[5]));
    return text;
}
std::string ip(const SOCKET_ADDRESS& a) {
    if (!a.lpSockaddr || a.iSockaddrLength < static_cast<int>(sizeof(sockaddr_in)) || a.lpSockaddr->sa_family != AF_INET) return {};
    const auto* v4 = reinterpret_cast<const sockaddr_in*>(a.lpSockaddr);
    char buffer[INET_ADDRSTRLEN]{};
    return InetNtopA(AF_INET, &v4->sin_addr, buffer, sizeof buffer) ? buffer : "";
}
std::string phy(DOT11_PHY_TYPE type) {
    // IEEE/native WLAN enumeration; keep any SDK/driver additions visible by numeric ID.
    switch (static_cast<unsigned>(type)) {
    case 4: return "OFDM"; case 5: return "HRDSSS"; case 6: return "ERP"; case 7: return "802.11n";
    case 8: return "802.11ac"; case 9: return "802.11ad"; case 10: return "802.11ax"; case 11: return "802.11be";
    default: return "PHY #" + std::to_string(type);
    }
}
unsigned channel(unsigned mhz) {
    if (mhz == 2484) return 14;
    if (mhz >= 2412 && mhz <= 2472 && (mhz - 2407) % 5 == 0) return (mhz - 2407) / 5;
    if (mhz >= 5005 && mhz <= 5895 && mhz % 5 == 0) return (mhz - 5000) / 5;
    if (mhz == 5935) return 2;
    if (mhz >= 5955 && mhz <= 7115 && (mhz - 5950) % 5 == 0) return (mhz - 5950) / 5;
    return 0;
}
Error fail(std::string op, DWORD code) {
    return {std::move(op), code, code == ERROR_ACCESS_DENIED ? "Access denied: check Windows Wi-Fi/location permissions and policy" : "Native Windows operation failed"};
}
struct Wlan {
    HANDLE value{};
    DWORD result{};
    Wlan() { DWORD selected{}; result = WlanOpenHandle(2, nullptr, &selected, &value); }
    ~Wlan() { if (value) WlanCloseHandle(value, nullptr); }
    Wlan(const Wlan&) = delete;
    Wlan& operator=(const Wlan&) = delete;
};
struct WlanFree { void operator()(void* value) const { WlanFreeMemory(value); } };
template<class T> using WlanPtr = std::unique_ptr<T, WlanFree>;
WlanPtr<WLAN_INTERFACE_INFO_LIST> interfaces(Wlan& wlan, std::vector<Error>& errors) {
    if (wlan.result) { errors.push_back(fail("WlanOpenHandle", wlan.result)); return {}; }
    WLAN_INTERFACE_INFO_LIST* ptr{};
    const auto status = WlanEnumInterfaces(wlan.value, nullptr, &ptr);
    if (status) errors.push_back(fail("WlanEnumInterfaces", status));
    return WlanPtr<WLAN_INTERFACE_INFO_LIST>(ptr);
}
template<class T> WlanPtr<T> query(HANDLE handle, const GUID& guid, WLAN_INTF_OPCODE opcode, DWORD& status) {
    DWORD size{}; void* raw{};
    status = WlanQueryInterface(handle, &guid, opcode, nullptr, &size, &raw, nullptr);
    if (status || size < sizeof(T)) {
        if (raw) WlanFreeMemory(raw);
        if (!status) status = ERROR_INVALID_DATA;
        return {};
    }
    return WlanPtr<T>(static_cast<T*>(raw));
}
void pnp(Snapshot& result, std::stop_token stop) {
    const GUID net_class{0x4d36e972, 0xe325, 0x11ce, {0xbf, 0xc1, 0x08, 0x00, 0x2b, 0xe1, 0x03, 0x18}};
    struct Set { HDEVINFO handle; ~Set() { if (handle != INVALID_HANDLE_VALUE) SetupDiDestroyDeviceInfoList(handle); } };
    const Set set{SetupDiGetClassDevsW(&net_class, nullptr, nullptr, DIGCF_PRESENT)};
    if (set.handle == INVALID_HANDLE_VALUE) { result.errors.push_back(fail("SetupDiGetClassDevs", GetLastError())); return; }
    SP_DEVINFO_DATA data{}; data.cbSize = sizeof data;
    for (DWORD i = 0; !stop.stop_requested() && SetupDiEnumDeviceInfo(set.handle, i, &data); ++i) {
        struct Key { HKEY handle; ~Key() { if (handle != INVALID_HANDLE_VALUE) RegCloseKey(handle); } };
        const Key key{SetupDiOpenDevRegKey(set.handle, &data, DICS_FLAG_GLOBAL, 0, DIREG_DRV, KEY_QUERY_VALUE)};
        if (key.handle == INVALID_HANDLE_VALUE) continue;
        const auto reg = [&](const wchar_t* name) {
            wchar_t value[512]{}; DWORD bytes = sizeof value;
            return RegGetValueW(key.handle, nullptr, name, RRF_RT_REG_SZ, nullptr, value, &bytes) == ERROR_SUCCESS ? utf8(value) : std::string{};
        };
        const auto id = reg(L"NetCfgInstanceId");
        if (id.empty()) continue;
        for (auto& adapter : result.interfaces) if (equal_id(adapter.id, id)) {
            adapter.driver = reg(L"DriverVersion"); adapter.provider = reg(L"ProviderName"); adapter.driver_date = reg(L"DriverDate");
            wchar_t props[2048]{};
            if (SetupDiGetDeviceRegistryPropertyW(set.handle, &data, SPDRP_HARDWAREID, nullptr,
                reinterpret_cast<BYTE*>(props), sizeof props, nullptr)) adapter.hardware_id = utf8(props);
            if (SetupDiGetDeviceRegistryPropertyW(set.handle, &data, SPDRP_MFG, nullptr,
                reinterpret_cast<BYTE*>(props), sizeof props, nullptr)) adapter.manufacturer = utf8(props);
        }
    }
}
Snapshot snapshot(std::stop_token stop) {
    Snapshot out;
    ULONG size = 15 * 1024;
    std::vector<std::max_align_t> storage;
    IP_ADAPTER_ADDRESSES* head{};
    for (int attempt = 0; attempt < 3 && !stop.stop_requested(); ++attempt) {
        storage.resize((size + sizeof(std::max_align_t) - 1) / sizeof(std::max_align_t));
        auto* current = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(storage.data());
        const auto code = GetAdaptersAddresses(AF_INET, GAA_FLAG_INCLUDE_GATEWAYS | GAA_FLAG_INCLUDE_ALL_INTERFACES,
            nullptr, current, &size);
        if (!code) { head = current; break; }
        if (code != ERROR_BUFFER_OVERFLOW || size > 4 * 1024 * 1024 || attempt == 2) { out.errors.push_back(fail("GetAdaptersAddresses", code)); break; }
    }
    for (auto* a = head; a && !stop.stop_requested(); a = a->Next) {
        Interface item;
        GUID id{};
        item.id = ConvertInterfaceLuidToGuid(&a->Luid, &id) == NO_ERROR ? guid_text(id) : a->AdapterName;
        item.index = a->IfIndex;
        item.name = utf8(a->FriendlyName); item.description = utf8(a->Description);
        item.up = a->OperStatus == IfOperStatusUp; item.dhcp = (a->Flags & IP_ADAPTER_DHCP_ENABLED) != 0;
        item.wireless = a->IfType == IF_TYPE_IEEE80211;
        item.rx_mbps = static_cast<double>(a->ReceiveLinkSpeed) / 1000000.; item.tx_mbps = static_cast<double>(a->TransmitLinkSpeed) / 1000000.;
        MIB_IF_ROW2 row{}; row.InterfaceLuid = a->Luid;
        if (a->IfType == IF_TYPE_SOFTWARE_LOOPBACK) item.kind = "Loopback";
        else if (a->IfType == IF_TYPE_TUNNEL) item.kind = "Tunnel";
        else if (GetIfEntry2(&row) == NO_ERROR) item.kind = row.InterfaceAndOperStatusFlags.HardwareInterface ? "Physical (reported)" : "Non-hardware (reported)";
        else item.kind = "Unknown";
        for (auto* u = a->FirstUnicastAddress; u; u = u->Next) {
            if (u->DadState != IpDadStatePreferred) continue;
            auto address = ip(u->Address);
            if (address.empty()) continue;
            if (item.address.empty() || item.address.starts_with("169.254.")) item.address = std::move(address);
            if (!item.address.starts_with("169.254.")) break;
        }
        if (a->FirstGatewayAddress) item.gateway = ip(a->FirstGatewayAddress->Address);
        if (a->FirstDnsServerAddress) item.dns = ip(a->FirstDnsServerAddress->Address);
        out.interfaces.push_back(std::move(item));
    }
    if (stop.stop_requested()) return out;
    Wlan wlan;
    if (const auto list = interfaces(wlan, out.errors)) for (DWORD i = 0; i < list->dwNumberOfItems && !stop.stop_requested(); ++i) {
        const auto& info = list->InterfaceInfo[i];
        auto id = guid_text(info.InterfaceGuid);
        auto match = std::find_if(out.interfaces.begin(), out.interfaces.end(), [&](const Interface& it) { return equal_id(it.id, id); });
        if (match == out.interfaces.end()) {
            out.interfaces.emplace_back(); match = out.interfaces.end() - 1;
            match->id = std::move(id); match->description = utf8(info.strInterfaceDescription); match->kind = "Unknown";
        }
        match->wireless = true;
        WLAN_INTERFACE_CAPABILITY* raw{};
        const auto code = WlanGetInterfaceCapability(wlan.value, &info.InterfaceGuid, nullptr, &raw);
        const WlanPtr<WLAN_INTERFACE_CAPABILITY> cap(raw);
        if (code) out.errors.push_back(fail("WlanGetInterfaceCapability", code));
        else if (cap) for (DWORD j = 0; j < (std::min)(cap->dwNumberOfSupportedPhys, static_cast<DWORD>(WLAN_MAX_PHY_INDEX)); ++j) {
            if (j) match->phy += ", "; match->phy += phy(cap->dot11PhyTypes[j]);
        }
        DWORD radio_status{};
        if (const auto radio = query<WLAN_RADIO_STATE>(wlan.value, info.InterfaceGuid, wlan_intf_opcode_radio_state, radio_status)) {
            unsigned on{}, off{};
            for (DWORD j = 0; j < (std::min)(radio->dwNumberOfPhys, static_cast<DWORD>(WLAN_MAX_PHY_INDEX)); ++j) {
                const auto& state = radio->PhyRadioState[j];
                if (state.dot11HardwareRadioState == dot11_radio_state_off || state.dot11SoftwareRadioState == dot11_radio_state_off) ++off;
                else if (state.dot11HardwareRadioState == dot11_radio_state_on && state.dot11SoftwareRadioState == dot11_radio_state_on) ++on;
            }
            if (on || off) match->radio = std::to_string(on) + " on / " + std::to_string(off) + " off PHY entries";
        }
        if (info.isState == wlan_interface_state_connected) {
            DWORD status{};
            if (const auto connection = query<WLAN_CONNECTION_ATTRIBUTES>(wlan.value, info.InterfaceGuid, wlan_intf_opcode_current_connection, status)) {
                const auto& a = connection->wlanAssociationAttributes;
                match->associated = true; match->profile = utf8(connection->strProfileName); match->ssid = label(a.dot11Ssid);
                match->bssid = mac(a.dot11Bssid); match->rx_mbps = a.ulRxRate / 1000.; match->tx_mbps = a.ulTxRate / 1000.;
                if (const auto rssi = query<LONG>(wlan.value, info.InterfaceGuid, wlan_intf_opcode_rssi, status)) match->rssi = static_cast<int>(*rssi);
            } else out.errors.push_back(fail("WlanQueryInterface(current connection)", status));
        }
    }
    pnp(out, stop);
    return out;
}
struct ScanNotice {
    GUID guid{};
    std::atomic<DWORD> result{};
    static void WINAPI callback(PWLAN_NOTIFICATION_DATA data, PVOID context) {
        auto& self = *static_cast<ScanNotice*>(context);
        if (data->NotificationSource == WLAN_NOTIFICATION_SOURCE_ACM && IsEqualGUID(data->InterfaceGuid, self.guid) &&
            (data->NotificationCode == wlan_notification_acm_scan_complete || data->NotificationCode == wlan_notification_acm_scan_fail)) {
            self.result.store(data->NotificationCode, std::memory_order_release);
        }
    }
};
struct Socket { SOCKET value{INVALID_SOCKET}; ~Socket() { if (value != INVALID_SOCKET) closesocket(value); } };
struct Winsock { WSADATA data{}; int code{WSAStartup(MAKEWORD(2, 2), &data)}; ~Winsock() { if (!code) WSACleanup(); } };
struct Icmp { HANDLE value{IcmpCreateFile()}; ~Icmp() { if (value != INVALID_HANDLE_VALUE) IcmpCloseHandle(value); } };
std::optional<double> echo(const Icmp& handle, IN_ADDR from, IN_ADDR to, unsigned timeout) {
    char payload[16]{};
    alignas(ICMP_ECHO_REPLY) char reply[sizeof(ICMP_ECHO_REPLY) + sizeof payload + 16]{};
    if (handle.value != INVALID_HANDLE_VALUE && IcmpSendEcho2Ex(handle.value, nullptr, nullptr, nullptr, from.S_un.S_addr,
        to.S_un.S_addr, payload, sizeof payload, nullptr, reply, sizeof reply, timeout)) {
        const auto* response = reinterpret_cast<const ICMP_ECHO_REPLY*>(reply);
        if (response->Status == IP_SUCCESS) return static_cast<double>(response->RoundTripTime);
    }
    return {};
}
bool ready(SOCKET socket, bool write, Clock::time_point end, std::stop_token stop) {
    while (!stop.stop_requested() && Clock::now() < end) {
        fd_set set; FD_ZERO(&set); FD_SET(socket, &set);
        timeval time{0, 25000};
        const int code = ::select(0, write ? nullptr : &set, write ? &set : nullptr, nullptr, &time);
        if (code) return code > 0;
    }
    return false;
}
std::optional<double> resolve(const Interface& i, const Scope& scope, sockaddr_in source, IN_ADDR endpoint, std::stop_token stop) {
    if (scope.dns_name.empty() || stop.stop_requested()) return {};
    const Socket connection{socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)};
    const auto s = connection.value;
    const DWORD index = htonl(i.index);
    sockaddr_in resolver{}; resolver.sin_family = AF_INET; resolver.sin_port = htons(53);
    if (s == INVALID_SOCKET || InetPtonA(AF_INET, i.dns.c_str(), &resolver.sin_addr) != 1 ||
        setsockopt(s, IPPROTO_IP, IP_UNICAST_IF, reinterpret_cast<const char*>(&index), sizeof index) ||
        bind(s, reinterpret_cast<const sockaddr*>(&source), sizeof source) ||
        ::connect(s, reinterpret_cast<const sockaddr*>(&resolver), sizeof resolver)) return {};
    u_long nonblocking = 1;
    if (ioctlsocket(s, FIONBIO, &nonblocking)) return {};
    const auto begin = Clock::now();
    const auto question = dns_query(scope.dns_name, static_cast<std::uint16_t>(begin.time_since_epoch().count()));
    const auto bytes = static_cast<int>(question.size());
    if (send(s, reinterpret_cast<const char*>(question.data()), bytes, 0) != bytes ||
        !ready(s, false, begin + std::chrono::milliseconds((std::min)(scope.timeout_ms, 800u)), stop)) return {};
    std::array<std::uint8_t,512> reply{};
    const int count = recv(s, reinterpret_cast<char*>(reply.data()), static_cast<int>(reply.size()), 0);
    const auto parsed = ntohl(endpoint.S_un.S_addr);
    const std::array ip_bytes{static_cast<std::uint8_t>(parsed >> 24), static_cast<std::uint8_t>(parsed >> 16),
        static_cast<std::uint8_t>(parsed >> 8), static_cast<std::uint8_t>(parsed)};
    if (count <= 0 || !dns_confirms(question, std::span(reply.data(), static_cast<std::size_t>(count)), ip_bytes)) return {};
    return std::chrono::duration<double, std::milli>(Clock::now() - begin).count();
}
std::optional<std::pair<double,double>> transfer(const Interface& i, const Scope& scope, sockaddr_in source, sockaddr_in target, std::stop_token stop) {
    if (stop.stop_requested()) return {};
    const Socket connection{socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)};
    const auto s = connection.value;
    if (s == INVALID_SOCKET) return {};
    const DWORD index = htonl(i.index);
    u_long nonblocking = 1;
    if (setsockopt(s, IPPROTO_IP, IP_UNICAST_IF, reinterpret_cast<const char*>(&index), sizeof index) ||
        ioctlsocket(s, FIONBIO, &nonblocking) || bind(s, reinterpret_cast<const sockaddr*>(&source), sizeof source)) return {};
    const auto deadline = Clock::now() + std::chrono::milliseconds(scope.timeout_ms);
    if (::connect(s, reinterpret_cast<const sockaddr*>(&target), sizeof target)) {
        if (WSAGetLastError() != WSAEWOULDBLOCK || !ready(s, true, deadline, stop)) return {};
        int code{}, length = sizeof code;
        if (getsockopt(s, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&code), &length) || code) return {};
    }
    auto write = [&](std::string_view data) {
        while (!data.empty() && ready(s, true, deadline, stop)) {
            const auto n = send(s, data.data(), static_cast<int>(data.size()), 0);
            if (n <= 0) return false;
            data.remove_prefix(static_cast<std::size_t>(n));
        }
        return data.empty();
    };
    auto read = [&](std::size_t size) {
        std::string response(size, '\0'); std::size_t offset{};
        while (offset < size && ready(s, false, deadline, stop)) {
            const auto n = recv(s, response.data() + offset, static_cast<int>(size - offset), 0);
            if (n <= 0) break;
            offset += static_cast<std::size_t>(n);
        }
        response.resize(offset); return response;
    };
    const auto nonce = static_cast<std::uint64_t>(Clock::now().time_since_epoch().count());
    std::string payload(scope.bytes, 'x');
    for (std::size_t k = 0; k < payload.size(); ++k) payload[k] = static_cast<char>(((nonce >> ((k % 8) * 8)) ^ k) & 0xff);
    if (!write("CLP1 " + std::to_string(scope.bytes) + "\n") || read(4) != "RDY1") return {};
    const auto start = Clock::now();
    if (!write(payload) || read(4) != "ACK1") return {};
    const auto uploaded = Clock::now();
    if (!write("GO1\n") || read(payload.size()) != payload) return {};
    const auto downloaded = Clock::now();
    const auto rate = [&](auto duration) { return scope.bytes * 8. / (1e6 * std::max(1e-6, std::chrono::duration<double>(duration).count())); };
    return std::pair{rate(uploaded - start), rate(downloaded - uploaded)};
}
class Windows final : public Backend {
public:
    Snapshot inspect(std::stop_token stop) override { return snapshot(stop); }
    Discovery scan(std::stop_token stop) override {
        Discovery out;
        Wlan wlan;
        const auto list = interfaces(wlan, out.errors);
        if (!list) return out;
        const auto scan_deadline = Clock::now() + 12s;
        for (DWORD i = 0; i < list->dwNumberOfItems && !stop.stop_requested(); ++i) {
            if (Clock::now() >= scan_deadline) { out.errors.push_back(fail("Discovery total budget", ERROR_TIMEOUT)); break; }
            const auto& info = list->InterfaceInfo[i];
            ScanNotice notice{info.InterfaceGuid};
            const auto registered = WlanRegisterNotification(wlan.value, WLAN_NOTIFICATION_SOURCE_ACM, TRUE, ScanNotice::callback, &notice, nullptr, nullptr);
            if (registered) { out.errors.push_back(fail("WlanRegisterNotification", registered)); continue; }
            const auto result = WlanScan(wlan.value, &info.InterfaceGuid, nullptr, nullptr, nullptr);
            if (!result) {
                const auto end = std::min(Clock::now() + 5s, scan_deadline);
                while (!notice.result.load(std::memory_order_acquire) && Clock::now() < end && !stop.stop_requested()) std::this_thread::sleep_for(40ms);
            }
            const auto unregistered = WlanRegisterNotification(wlan.value, WLAN_NOTIFICATION_SOURCE_NONE, TRUE, nullptr, nullptr, nullptr, nullptr);
            if (unregistered) {
                out.errors.push_back(fail("WlanRegisterNotification(unregister)", unregistered));
                WlanCloseHandle(wlan.value, nullptr); wlan.value = nullptr;
                break;
            }
            if (stop.stop_requested()) break;
            if (result) { out.errors.push_back(fail("WlanScan", result)); continue; }
            if (notice.result.load() != wlan_notification_acm_scan_complete) {
                out.errors.push_back({"WlanScan", static_cast<unsigned>(notice.result.load() ? ERROR_GEN_FAILURE : ERROR_TIMEOUT), "No successful fresh scan notification"}); continue;
            }
            WLAN_AVAILABLE_NETWORK_LIST* raw_profiles{};
            const auto profiles_status = WlanGetAvailableNetworkList(wlan.value, &info.InterfaceGuid, 0, nullptr, &raw_profiles);
            const WlanPtr<WLAN_AVAILABLE_NETWORK_LIST> profiles(raw_profiles);
            if (profiles_status) out.errors.push_back(fail("WlanGetAvailableNetworkList", profiles_status));
            WLAN_BSS_LIST* raw_bss{};
            const auto code = WlanGetNetworkBssList(wlan.value, &info.InterfaceGuid, nullptr, dot11_BSS_type_any, FALSE, nullptr, &raw_bss);
            const WlanPtr<WLAN_BSS_LIST> entries(raw_bss);
            if (code) { out.errors.push_back(fail("WlanGetNetworkBssList", code)); continue; }
            if (!entries) continue;
            for (DWORD j = 0; j < entries->dwNumberOfItems && out.bss.size() < 2048 && !stop.stop_requested(); ++j) {
                const auto& e = entries->wlanBssEntries[j];
                Bss b{guid_text(info.InterfaceGuid), label(e.dot11Ssid), mac(e.dot11Bssid),
                    (e.usCapabilityInformation & 0x10) ? "Privacy bit set (suite unverified)" : "No privacy bit", {},
                    static_cast<int>(e.lRssi), static_cast<unsigned>(e.ulChCenterFrequency / 1000), 0};
                b.channel = channel(b.mhz);
                if (profiles) for (DWORD k = 0; k < profiles->dwNumberOfItems; ++k) {
                    const auto& p = profiles->Network[k];
                    if (p.dot11BssType == e.dot11BssType && label(p.dot11Ssid) == b.ssid && p.strProfileName[0] &&
                        (p.bSecurityEnabled != FALSE) == ((e.usCapabilityInformation & 0x10) != 0)) {
                        b.profile = utf8(p.strProfileName); break;
                    }
                }
                out.bss.push_back(std::move(b));
            }
        }
        return out;
    }
    std::optional<Error> connect(const Bss& b, std::stop_token stop) override {
        if (stop.stop_requested()) return Error{"WlanConnect", ERROR_CANCELLED, "Cancelled before connection"};
        Wlan wlan;
        std::vector<Error> errors;
        const auto list = interfaces(wlan, errors);
        if (!list) return errors.empty() ? fail("WlanEnumInterfaces", ERROR_NOT_FOUND) : errors.front();
        for (DWORD i = 0; i < list->dwNumberOfItems; ++i) {
            const auto& info = list->InterfaceInfo[i];
            if (!equal_id(guid_text(info.InterfaceGuid), b.interface_id)) continue;
            wchar_t profile[WLAN_MAX_NAME_LENGTH]{};
            if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, b.profile.c_str(), -1, profile, WLAN_MAX_NAME_LENGTH)) return fail("Profile UTF-8", ERROR_INVALID_DATA);
            DOT11_BSSID_LIST desired{};
            desired.Header = {NDIS_OBJECT_TYPE_DEFAULT, DOT11_BSSID_LIST_REVISION_1, sizeof desired};
            desired.uNumOfEntries = desired.uTotalNumOfEntries = 1;
            if (b.bssid.size() != 17) return fail("BSSID selection", ERROR_INVALID_DATA);
            for (unsigned k = 0; k < 6; ++k) {
                const auto* start = b.bssid.data() + k * 3;
                unsigned value{}; const auto parsed = std::from_chars(start, start + 2, value, 16);
                if (parsed.ec != std::errc{} || parsed.ptr != start + 2 || (k < 5 && start[2] != ':')) return fail("BSSID selection", ERROR_INVALID_DATA);
                desired.BSSIDs[0][k] = static_cast<UCHAR>(value);
            }
            WLAN_CONNECTION_PARAMETERS params{wlan_connection_mode_profile, profile, nullptr, &desired, dot11_BSS_type_infrastructure, 0};
            const auto code = WlanConnect(wlan.value, &info.InterfaceGuid, &params, nullptr);
            if (code) return fail("WlanConnect", code);
            const auto end = Clock::now() + 15s;
            while (Clock::now() < end && !stop.stop_requested()) {
                const auto current = snapshot(stop);
                for (const auto& item : current.interfaces) if (equal_id(item.id, b.interface_id) && item.associated &&
                    item.profile == b.profile && item.bssid == b.bssid && !item.address.empty() && !item.address.starts_with("169.254.") && !item.gateway.empty()) return {};
                std::this_thread::sleep_for(150ms);
            }
            if (stop.stop_requested()) {
                // Cancel the host-owned request when possible; never deauthenticate other stations.
                WlanDisconnect(wlan.value, &info.InterfaceGuid, nullptr);
                return fail("WlanConnect cancelled", ERROR_CANCELLED);
            }
            return Error{"WlanConnect", ERROR_TIMEOUT, "Profile/BSSID or usable host IPv4 not confirmed within 15 s"};
        }
        return fail("WlanConnect interface", ERROR_NOT_FOUND);
    }
    Observation measure(const Interface& i, const Scope& scope, std::stop_token stop) override {
        Observation o;
        if (!scope.valid() || !i.index || i.profile != scope.profile || i.bssid != scope.bssid) {
            o.error = Error{"scope", ERROR_ACCESS_DENIED, "No matching active WLAN interface, profile and BSSID"}; return o;
        }
        const Winsock wsa;
        if (wsa.code) { o.error = fail("WSAStartup", static_cast<DWORD>(wsa.code)); return o; }
        sockaddr_in source{}, target{}; source.sin_family = target.sin_family = AF_INET;
        target.sin_port = htons(static_cast<u_short>(scope.port));
        if (InetPtonA(AF_INET, i.address.c_str(), &source.sin_addr) != 1 || InetPtonA(AF_INET, scope.endpoint.c_str(), &target.sin_addr) != 1) {
            o.error = fail("IPv4 parsing", ERROR_INVALID_DATA); return o;
        }
        SOCKADDR_INET destination{}; destination.Ipv4 = target;
        SOCKADDR_INET local{}; local.Ipv4 = source;
        MIB_IPFORWARD_ROW2 route{}; SOCKADDR_INET best_source{};
        const auto route_code = GetBestRoute2(nullptr, i.index, &local, &destination, 0, &route, &best_source);
        if (route_code || route.InterfaceIndex != i.index || best_source.Ipv4.sin_addr.S_un.S_addr != source.sin_addr.S_un.S_addr) {
            o.error = fail("GetBestRoute2 bound IPv4", route_code ? route_code : ERROR_NETWORK_UNREACHABLE); return o;
        }
        o.dns_ms = resolve(i, scope, source, target.sin_addr, stop);
        const Icmp ping;
        IN_ADDR gateway{};
        if (!stop.stop_requested() && InetPtonA(AF_INET, i.gateway.c_str(), &gateway) == 1) o.gateway_replied = echo(ping, source.sin_addr, gateway, 350).has_value();
        double jitter{}, sum{}, previous{};
        for (unsigned n = 0; n < scope.repetitions && !stop.stop_requested() && ping.value != INVALID_HANDLE_VALUE; ++n) {
            ++o.attempts;
            if (const auto response = echo(ping, source.sin_addr, target.sin_addr, (std::min)(scope.timeout_ms, 350u))) {
                if (o.replies) jitter += std::abs(*response - previous);
                ++o.replies; sum += *response; previous = *response; o.latency_history.push_back(static_cast<float>(*response));
            }
            if (!stop.stop_requested()) std::this_thread::sleep_for(60ms);
        }
        if (o.attempts) { o.stability = 100. * o.replies / o.attempts; o.loss_percent = 100. - *o.stability; }
        if (o.replies) o.latency_ms = sum / o.replies;
        if (o.replies > 1) o.jitter_ms = jitter / (o.replies - 1);
        if (const auto rates = transfer(i, scope, source, target, stop)) {
            const auto observed = snapshot(stop);
            const auto same = [&](const Interface& after) { return after.index == i.index && after.id == i.id && after.bssid == i.bssid &&
                after.profile == i.profile && after.address == i.address && after.associated; };
            if (std::none_of(observed.interfaces.begin(), observed.interfaces.end(), same)) {
                o.error = {"Interface consistency", ERROR_NOT_READY, "Interface or association changed during experiment; observations discarded"};
                Observation unavailable; unavailable.error = o.error; return unavailable;
            }
            o.endpoint_verified = true; o.up_mbps = rates->first; o.down_mbps = rates->second; o.bytes = scope.bytes * 2;
        } else o.error = {"CLP1", static_cast<unsigned>(stop.stop_requested() ? ERROR_CANCELLED : ERROR_TIMEOUT), "Bound controlled endpoint did not complete protocol within limits"};
        return o;
    }
};
}
Backend& platform() { static Windows backend; return backend; }
}
