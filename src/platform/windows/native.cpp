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
#include <netlistmgr.h>
#include <ocidl.h>
#include <oleauto.h>

#include "campuslink/network.hpp"
#include "campuslink/dns.hpp"
#include <algorithm>
#include <atomic>
#include <charconv>
#include <cstddef>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <numeric>
#include <string>
#include <string_view>
#include <thread>

namespace campuslink::net {
namespace {
using namespace std::chrono_literals;
std::string utf8(const wchar_t* wide) {
    if (!wide) return {};
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
            wchar_t instance[512]{};
            if (SetupDiGetDeviceInstanceIdW(set.handle, &data, instance, static_cast<DWORD>(std::size(instance)), nullptr)) adapter.device_instance = utf8(instance);
            adapter.driver = reg(L"DriverVersion"); adapter.provider = reg(L"ProviderName"); adapter.driver_date = reg(L"DriverDate");
            wchar_t props[2048]{};
            if (SetupDiGetDeviceRegistryPropertyW(set.handle, &data, SPDRP_HARDWAREID, nullptr,
                reinterpret_cast<BYTE*>(props), sizeof props, nullptr)) adapter.hardware_id = utf8(props);
            if (SetupDiGetDeviceRegistryPropertyW(set.handle, &data, SPDRP_MFG, nullptr,
                reinterpret_cast<BYTE*>(props), sizeof props, nullptr)) adapter.manufacturer = utf8(props);
        }
    }
}
Snapshot snapshot(std::stop_token stop, bool details = true) {
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
        else if (GetIfEntry2(&row) == NO_ERROR) {
            item.hardware_interface = row.InterfaceAndOperStatusFlags.HardwareInterface;
            item.kind = item.hardware_interface ? "Physical (reported)" : "Non-hardware (reported)";
        }
        else item.kind = "Unknown";
        for (auto* u = a->FirstUnicastAddress; u; u = u->Next) {
            if (u->DadState != IpDadStatePreferred) continue;
            auto address = ip(u->Address);
            if (address.empty()) continue;
            if (item.address.empty() || item.address.starts_with("169.254.")) item.address = std::move(address);
            if (!item.address.starts_with("169.254.")) break;
        }
        for (auto* gateway = a->FirstGatewayAddress; gateway && item.gateway.empty(); gateway = gateway->Next) item.gateway = ip(gateway->Address);
        for (auto* dns = a->FirstDnsServerAddress; dns && item.dns.empty(); dns = dns->Next) item.dns = ip(dns->Address);
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
        match->wlan_idle = info.isState == wlan_interface_state_disconnected;
        if (details) {
            WLAN_INTERFACE_CAPABILITY* raw{};
            const auto code = WlanGetInterfaceCapability(wlan.value, &info.InterfaceGuid, nullptr, &raw);
            const WlanPtr<WLAN_INTERFACE_CAPABILITY> cap(raw);
            if (code) out.errors.push_back(fail("WlanGetInterfaceCapability", code));
            else if (cap) {
                match->wlan_interface_type = cap->interfaceType == wlan_interface_type_native_802_11 ? "Native 802.11" :
                    cap->interfaceType == wlan_interface_type_emulated_802_11 ? "Emulated 802.11" : "Unknown";
                match->desired_bssids = cap->dwMaxDesiredBssidListSize;
                match->dot11d_capable = cap->bDot11DSupported != FALSE;
                for (DWORD j = 0; j < (std::min)(cap->dwNumberOfSupportedPhys, static_cast<DWORD>(WLAN_MAX_PHY_INDEX)); ++j) {
                    if (j) match->phy += ", ";
                    match->phy += phy(cap->dot11PhyTypes[j]);
                }
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
            DWORD status{};
            if (const auto op = query<ULONG>(wlan.value, info.InterfaceGuid, wlan_intf_opcode_current_operation_mode, status)) {
                for (const auto& [mask, label] : std::array<std::pair<ULONG, const char*>, 7>{{
                    {1, "Station"}, {2, "AP"}, {4, "Extensible station"}, {8, "Extensible AP"},
                    {16, "Wi-Fi Direct device"}, {32, "Wi-Fi Direct group owner"}, {64, "Wi-Fi Direct client"}}}) {
                    if (*op & mask) { if (!match->active_role.empty()) match->active_role += ", "; match->active_role += label; }
                }
            }
            if (const auto hosted = query<BOOL>(wlan.value, info.InterfaceGuid, wlan_intf_opcode_hosted_network_capable, status))
                match->hosted_network_capable = *hosted != FALSE;
            if (const auto mfp = query<BOOL>(wlan.value, info.InterfaceGuid, wlan_intf_opcode_management_frame_protection_capable, status))
                match->mfp_capable = *mfp != FALSE;
            if (const auto ch = query<ULONG>(wlan.value, info.InterfaceGuid, wlan_intf_opcode_channel_number, status)) match->channel = *ch;
            // Microsoft documents WLAN_INTERFACE_INFO_LIST for this opcode.
            void* payload{}; DWORD bytes{};
            const auto rc = WlanQueryInterface(wlan.value, &info.InterfaceGuid, wlan_intf_opcode_secondary_sta_interfaces,
                nullptr, &bytes, &payload, nullptr);
            const WlanPtr<WLAN_INTERFACE_INFO_LIST> secondary(static_cast<WLAN_INTERFACE_INFO_LIST*>(payload));
            constexpr auto header = offsetof(WLAN_INTERFACE_INFO_LIST, InterfaceInfo);
            if (rc) match->secondary_sta_query = fail("WlanQueryInterface(secondary STA)", rc);
            else if (!secondary || bytes < header || secondary->dwNumberOfItems > (bytes - header) / sizeof(WLAN_INTERFACE_INFO) ||
                     secondary->dwNumberOfItems > 64) {
                match->secondary_sta_query = fail("WlanQueryInterface(secondary STA)", ERROR_INVALID_DATA);
            } else {
                match->secondary_sta_reported = true;
                match->secondary_sta_bytes = bytes;
                const auto* entries = reinterpret_cast<const std::byte*>(secondary.get()) + header;
                for (DWORD j = 0; j < secondary->dwNumberOfItems; ++j) {
                    WLAN_INTERFACE_INFO entry{};
                    std::memcpy(&entry, entries + j * sizeof entry, sizeof entry);
                    match->secondary_sta.push_back({guid_text(entry.InterfaceGuid), static_cast<unsigned>(entry.isState)});
                }
            }
            if (const auto sync = query<BOOL>(wlan.value, info.InterfaceGuid, wlan_intf_opcode_secondary_sta_synchronized_connections, status))
                match->secondary_synchronized = *sync != FALSE;
        }
        if (info.isState == wlan_interface_state_connected) {
            DWORD status{};
            if (const auto connection = query<WLAN_CONNECTION_ATTRIBUTES>(wlan.value, info.InterfaceGuid, wlan_intf_opcode_current_connection, status)) {
                const auto& a = connection->wlanAssociationAttributes;
                match->associated = true; match->profile = utf8(connection->strProfileName); match->ssid = label(a.dot11Ssid);
                match->bssid = mac(a.dot11Bssid); match->current_phy = phy(a.dot11PhyType); match->rx_mbps = a.ulRxRate / 1000.; match->tx_mbps = a.ulTxRate / 1000.;
                if (const auto rssi = query<LONG>(wlan.value, info.InterfaceGuid, wlan_intf_opcode_rssi, status)) match->rssi = static_cast<int>(*rssi);
            } else out.errors.push_back(fail("WlanQueryInterface(current connection)", status));
        }
    }
    if (details) {
        pnp(out, stop);
        for (auto& item : out.interfaces) {
            if (item.wireless && item.description.find("AX201") != std::string::npos && item.description.find("Intel") != std::string::npos)
                item.documented_radio = "Intel AX201 documentation: Wi-Fi 6 (802.11ax), 2.4 / 5 GHz, 2x2, up to 160 MHz. Not verified for this unit.";
        }
    }
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
// Connection outcome reported by the auto-configuration module for one interface and profile.
// Callbacks run on a WLAN service thread; every field read across threads is atomic.
struct ConnectNotice {
    GUID guid{};
    std::wstring profile;
    std::atomic<bool> completed{}, disconnected{};
    std::atomic<DWORD> reason{}, last_attempt_reason{};
    ConnectNotice(const GUID& g, const wchar_t* p) : guid(g), profile(p) {}
    static void WINAPI callback(PWLAN_NOTIFICATION_DATA data, PVOID context) {
        auto& self = *static_cast<ConnectNotice*>(context);
        if (!data || data->NotificationSource != WLAN_NOTIFICATION_SOURCE_ACM || !IsEqualGUID(data->InterfaceGuid, self.guid)) return;
        const auto code = data->NotificationCode;
        if (code == wlan_notification_acm_disconnected) { self.disconnected.store(true, std::memory_order_release); return; }
        if (code != wlan_notification_acm_connection_complete && code != wlan_notification_acm_connection_attempt_fail) return;
        if (!data->pData || data->dwDataSize < sizeof(WLAN_CONNECTION_NOTIFICATION_DATA)) return;
        const auto* info = static_cast<const WLAN_CONNECTION_NOTIFICATION_DATA*>(data->pData);
        if (std::wstring_view(info->strProfileName, wcsnlen(info->strProfileName, WLAN_MAX_NAME_LENGTH)) != self.profile) return;
        if (code == wlan_notification_acm_connection_attempt_fail) {
            self.last_attempt_reason.store(info->wlanReasonCode, std::memory_order_release);
            return;
        }
        self.reason.store(info->wlanReasonCode, std::memory_order_release);
        if (info->wlanReasonCode != WLAN_REASON_CODE_SUCCESS) self.last_attempt_reason.store(info->wlanReasonCode, std::memory_order_release);
        self.completed.store(true, std::memory_order_release);
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
std::optional<std::pair<double,double>> transfer(const Interface& i, const Scope& scope, unsigned bytes, sockaddr_in source, sockaddr_in target, std::stop_token stop) {
    if (stop.stop_requested()) return {};
    const Socket connection{socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)};
    const auto s = connection.value;
    if (s == INVALID_SOCKET) return {};
    const DWORD index = htonl(i.index);
    u_long nonblocking = 1;
    if ((i.index && setsockopt(s, IPPROTO_IP, IP_UNICAST_IF, reinterpret_cast<const char*>(&index), sizeof index)) ||
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
    std::string payload(bytes, 'x');
    for (std::size_t k = 0; k < payload.size(); ++k) payload[k] = static_cast<char>(((nonce >> ((k % 8) * 8)) ^ k) & 0xff);
    if (!write("CLP1 " + std::to_string(bytes) + "\n") || read(4) != "RDY1") return {};
    const auto start = Clock::now();
    if (!write(payload) || read(4) != "ACK1") return {};
    const auto uploaded = Clock::now();
    if (!write("GO1\n") || read(payload.size()) != payload) return {};
    const auto downloaded = Clock::now();
    const auto rate = [&](auto duration) { return bytes * 8. / (1e6 * std::max(1e-6, std::chrono::duration<double>(duration).count())); };
    return std::pair{rate(uploaded - start), rate(downloaded - uploaded)};
}
bool settle(std::chrono::milliseconds wait, std::stop_token stop) {
    const auto until = Clock::now() + wait;
    while (!stop.stop_requested() && Clock::now() < until) std::this_thread::sleep_for(20ms);
    return !stop.stop_requested();
}
class Windows final : public Backend {
public:
    Snapshot inspect(std::stop_token stop) override { return snapshot(stop); }
    Discovery scan(std::stop_token stop) override {
        Discovery out;
        Wlan wlan;
        const auto list = interfaces(wlan, out.errors);
        if (!list) return out;
        // A full 2.4/5/6 GHz scan in a dense environment regularly exceeds 5 s on Intel AX2xx.
        const auto scan_deadline = Clock::now() + 15s;
        for (DWORD i = 0; i < list->dwNumberOfItems && !stop.stop_requested(); ++i) {
            if (Clock::now() >= scan_deadline) { out.errors.push_back(fail("Discovery total budget", ERROR_TIMEOUT)); break; }
            const auto& info = list->InterfaceInfo[i];
            ScanNotice notice{info.InterfaceGuid};
            const auto registered = WlanRegisterNotification(wlan.value, WLAN_NOTIFICATION_SOURCE_ACM, TRUE, ScanNotice::callback, &notice, nullptr, nullptr);
            if (registered) { out.errors.push_back(fail("WlanRegisterNotification", registered)); continue; }
            const auto result = WlanScan(wlan.value, &info.InterfaceGuid, nullptr, nullptr, nullptr);
            if (!result) {
                const auto end = std::min(Clock::now() + 8s, scan_deadline);
                while (!notice.result.load(std::memory_order_acquire) && Clock::now() < end && !stop.stop_requested()) std::this_thread::sleep_for(40ms);
            }
            const auto unregistered = WlanRegisterNotification(wlan.value, WLAN_NOTIFICATION_SOURCE_NONE, TRUE, nullptr, nullptr, nullptr, nullptr);
            if (unregistered) {
                out.errors.push_back(fail("WlanRegisterNotification(unregister)", unregistered));
                WlanCloseHandle(wlan.value, nullptr); wlan.value = nullptr;
                break;
            }
            if (stop.stop_requested()) break;
            // Without a confirmed fresh scan, still read the list Windows maintains from its own
            // background scans, keeping only entries heard recently (host timestamp below).
            if (result || notice.result.load() != wlan_notification_acm_scan_complete) out.cached = true;
            WLAN_AVAILABLE_NETWORK_LIST* raw_profiles{};
            const auto profiles_status = WlanGetAvailableNetworkList(wlan.value, &info.InterfaceGuid, 0, nullptr, &raw_profiles);
            const WlanPtr<WLAN_AVAILABLE_NETWORK_LIST> profiles(raw_profiles);
            if (profiles_status) out.errors.push_back(fail("WlanGetAvailableNetworkList", profiles_status));
            WLAN_BSS_LIST* raw_bss{};
            const auto code = WlanGetNetworkBssList(wlan.value, &info.InterfaceGuid, nullptr, dot11_BSS_type_any, FALSE, nullptr, &raw_bss);
            const WlanPtr<WLAN_BSS_LIST> entries(raw_bss);
            if (code) { out.errors.push_back(fail("WlanGetNetworkBssList", code)); continue; }
            if (!entries) continue;
            FILETIME now_file{};
            GetSystemTimeAsFileTime(&now_file);
            const auto now_100ns = (static_cast<ULONGLONG>(now_file.dwHighDateTime) << 32) | now_file.dwLowDateTime;
            constexpr ULONGLONG max_age_100ns = 120ull * 10'000'000ull;
            for (DWORD j = 0; j < entries->dwNumberOfItems && out.bss.size() < 2048 && !stop.stop_requested(); ++j) {
                const auto& e = entries->wlanBssEntries[j];
                // ullHostTimestamp: host time (FILETIME) when the beacon/probe response was received.
                if (e.ullHostTimestamp && e.ullHostTimestamp < now_100ns && now_100ns - e.ullHostTimestamp > max_age_100ns) { ++out.stale_dropped; continue; }
                Bss b{guid_text(info.InterfaceGuid), label(e.dot11Ssid), mac(e.dot11Bssid),
                    (e.usCapabilityInformation & 0x10) ? "Privacy bit set (suite unverified)" : "No privacy bit", {},
                    static_cast<int>(e.lRssi), static_cast<unsigned>(e.ulChCenterFrequency / 1000), 0};
                b.channel = channel(b.mhz);
                b.infrastructure = e.dot11BssType == dot11_BSS_type_infrastructure;
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
    Reachability reachability(const Interface& adapter) override {
        const auto initialized = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        if (FAILED(initialized) && initialized != RPC_E_CHANGED_MODE) return Reachability::unknown;
        struct Apartment { HRESULT status; ~Apartment() { if (SUCCEEDED(status)) CoUninitialize(); } } apartment{initialized};
        const auto release = [](auto* object) { if (object) object->Release(); };
        // CLSID_NetworkListManager and IID_INetworkListManager from netlistmgr.idl.
        // Use typed GUID values rather than the compiler extension __uuidof.
        constexpr GUID clsid{0xdcb00c01, 0x570f, 0x4a9b, {0x8d, 0x69, 0x19, 0x9f, 0xdb, 0xa5, 0x72, 0x3b}};
        constexpr GUID iid{0xdcb00000, 0x570f, 0x4a9b, {0x8d, 0x69, 0x19, 0x9f, 0xdb, 0xa5, 0x72, 0x3b}};
        // IID_IPropertyBag (ocidl.idl).
        constexpr GUID iid_bag{0x55272a00, 0x42cb, 0x11ce, {0x81, 0x35, 0x00, 0xaa, 0x00, 0x4b, 0xb8, 0x51}};
        void* raw_manager{};
        if (FAILED(CoCreateInstance(clsid, nullptr, CLSCTX_ALL, iid, &raw_manager)) || !raw_manager) return Reachability::unknown;
        const std::unique_ptr<INetworkListManager, decltype(release)> manager(static_cast<INetworkListManager*>(raw_manager), release);
        IEnumNetworkConnections* raw_list{};
        if (FAILED(manager->GetNetworkConnections(&raw_list)) || !raw_list) return Reachability::unknown;
        const std::unique_ptr<IEnumNetworkConnections, decltype(release)> links(raw_list, release);
        Reachability evidence{Reachability::unknown};
        for (unsigned n = 0; n < 128; ++n) {
            INetworkConnection* raw_connection{};
            ULONG written{};
            if (links->Next(1, &raw_connection, &written) != S_OK || !written || !raw_connection) break;
            const std::unique_ptr<INetworkConnection, decltype(release)> connection(raw_connection, release);
            GUID id{};
            NLM_CONNECTIVITY classification{};
            if (FAILED(connection->GetAdapterId(&id)) || !equal_id(guid_text(id), adapter.id) ||
                FAILED(connection->GetConnectivity(&classification))) continue;
            // NCSI marks a captive portal as WEBHIJACK in NA_InternetConnectivityV4/V6 (Windows 10 1709+).
            // A portal can coexist with a stale INTERNET bit, so check it first.
            void* raw_bag{};
            if (SUCCEEDED(connection->QueryInterface(iid_bag, &raw_bag)) && raw_bag) {
                const std::unique_ptr<IPropertyBag, decltype(release)> bag(static_cast<IPropertyBag*>(raw_bag), release);
                for (const auto* name : {L"NA_InternetConnectivityV4", L"NA_InternetConnectivityV6"}) {
                    VARIANT value; VariantInit(&value);
                    if (SUCCEEDED(bag->Read(name, &value, nullptr)) && (value.vt == VT_UI4 || value.vt == VT_I4) &&
                        (static_cast<unsigned long>(value.vt == VT_UI4 ? value.ulVal : static_cast<unsigned long>(value.lVal)) & NLM_INTERNET_CONNECTIVITY_WEBHIJACK)) {
                        VariantClear(&value);
                        return Reachability::portal;
                    }
                    VariantClear(&value);
                }
            }
            if (classification & (NLM_CONNECTIVITY_IPV4_INTERNET | NLM_CONNECTIVITY_IPV6_INTERNET)) return Reachability::internet;
            if (classification != NLM_CONNECTIVITY_DISCONNECTED) evidence = Reachability::local_only;
        }
        return evidence;
    }
    std::optional<Error> connect(const Bss& b, std::stop_token stop) override { return join(b, true, stop); }
    std::optional<Error> connect_profile(const Bss& b, std::stop_token stop) override { return join(b, false, stop); }
private:
    std::optional<Error> request_wifi(const Bss& b, WifiMode mode, std::stop_token stop, WifiTiming& timing,
                                      const WifiBudget& budget, const WifiProgressSink& progress) override {
        return join(b, mode == WifiMode::assisted, stop, &timing, budget, progress);
    }
    static Error reported(DWORD reason, std::string detail, WifiFailure fallback) {
        Error e{"WlanConnect", reason, std::move(detail), false, fallback, reason, {}};
        if (reason) {
            const auto mapped = classify_wlan_reason(reason);
            if (mapped != WifiFailure::unspecified) e.wifi_failure = mapped;
            wchar_t text[512]{};
            if (WlanReasonCodeToString(reason, 512, text, nullptr) == ERROR_SUCCESS) e.os_message = utf8(text);
        }
        return e;
    }
    // Phase A waits for Windows to report the outcome of this association (notification or
    // observed state); phase B waits for a stable usable IPv4 address and gateway. Only phase A
    // is ever cancelled at adapter level: once associated, the address is Windows' business.
    std::optional<Error> join(const Bss& b, bool exact, std::stop_token stop, WifiTiming* timing = nullptr,
                              const WifiBudget& budget = {}, const WifiProgressSink& progress = {}) {
        const auto start = Clock::now();
        const auto elapsed = [&] { return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - start); };
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
            if (exact && b.bssid.size() != 17) return fail("BSSID selection", ERROR_INVALID_DATA);
            for (unsigned k = 0; exact && k < 6; ++k) {
                const auto* start = b.bssid.data() + k * 3;
                unsigned value{}; const auto parsed = std::from_chars(start, start + 2, value, 16);
                if (parsed.ec != std::errc{} || parsed.ptr != start + 2 || (k < 5 && start[2] != ':')) return fail("BSSID selection", ERROR_INVALID_DATA);
                desired.BSSIDs[0][k] = static_cast<UCHAR>(value);
            }
            DOT11_SSID target_ssid{};
            // Reverse label() losslessly, so a profile listing multiple SSIDs cannot
            // silently try a different name from the one explicitly selected.
            const std::string_view ssid = b.ssid;
            for (std::size_t j = 0; j < ssid.size();) {
                if (target_ssid.uSSIDLength == DOT11_SSID_MAX_LENGTH) return fail("SSID", ERROR_INVALID_DATA);
                unsigned value = static_cast<unsigned char>(ssid[j++]);
                if (value == '\\') {
                    if (j + 3 > ssid.size() || ssid[j++] != 'x') return fail("SSID", ERROR_INVALID_DATA);
                    const auto parsed = std::from_chars(ssid.data() + j, ssid.data() + j + 2, value, 16);
                    if (parsed.ec != std::errc{} || parsed.ptr != ssid.data() + j + 2) return fail("SSID", ERROR_INVALID_DATA);
                    j += 2;
                }
                target_ssid.ucSSID[target_ssid.uSSIDLength++] = static_cast<UCHAR>(value);
            }
            ConnectNotice notice(info.InterfaceGuid, profile);
            const auto registered = WlanRegisterNotification(wlan.value, WLAN_NOTIFICATION_SOURCE_ACM, TRUE, ConnectNotice::callback, &notice, nullptr, nullptr);
            // Unregistering waits for in-flight callbacks, so `notice` outlives every callback.
            struct Unregister {
                HANDLE handle; bool active;
                ~Unregister() { if (active) WlanRegisterNotification(handle, WLAN_NOTIFICATION_SOURCE_NONE, TRUE, nullptr, nullptr, nullptr, nullptr); }
            } unregister{wlan.value, registered == ERROR_SUCCESS};
            WLAN_CONNECTION_PARAMETERS params{wlan_connection_mode_profile, profile, target_ssid.uSSIDLength ? &target_ssid : nullptr,
                exact ? &desired : nullptr, dot11_BSS_type_infrastructure, 0};
            const auto code = WlanConnect(wlan.value, &info.InterfaceGuid, &params, nullptr);
            if (code) { auto e = fail("WlanConnect", code); e.wifi_failure = WifiFailure::request; return e; }
            const auto matches = [&](const Interface& item) {
                return equal_id(item.id, b.interface_id) && item.associated && item.profile == b.profile &&
                    (b.ssid.empty() || item.ssid == b.ssid) && (!exact || item.bssid == b.bssid);
            };
            const auto current_match = [&](Snapshot& holder) -> const Interface* {
                holder = snapshot(stop, false);
                const auto it = std::find_if(holder.interfaces.begin(), holder.interfaces.end(), matches);
                return it == holder.interfaces.end() ? nullptr : &*it;
            };
            // Phase A: association and authentication.
            bool associated{};
            const auto associate_until = Clock::now() + budget.association;
            while (Clock::now() < associate_until && !stop.stop_requested()) {
                if (notice.completed.load(std::memory_order_acquire)) {
                    const auto reason = notice.reason.load(std::memory_order_acquire);
                    if (reason != WLAN_REASON_CODE_SUCCESS)
                        return reported(reason, "Windows reported that the connection did not complete", WifiFailure::association_unconfirmed);
                }
                Snapshot state;
                if (current_match(state)) { associated = true; break; }
                if (!settle(250ms, stop)) break;
            }
            if (!associated) {
                // Request still pending (no completion, no association). Cancel it at adapter level
                // unless Windows or the user already moved the adapter elsewhere.
                const auto latest = snapshot({}, false);
                const auto live = std::find_if(latest.interfaces.begin(), latest.interfaces.end(), [&](const auto& item) {
                    return equal_id(item.id, b.interface_id);
                });
                const bool another_link = live != latest.interfaces.end() && live->associated && !matches(*live);
                const bool idle = live != latest.interfaces.end() && live->wlan_idle;
                if (!another_link && (!idle || stop.stop_requested())) {
                    const auto disconnect = WlanDisconnect(wlan.value, &info.InterfaceGuid, nullptr);
                    if (disconnect != ERROR_SUCCESS) {
                        auto e = fail("WlanDisconnect", disconnect);
                        e.request_unsettled = true;
                        return e;
                    }
                    // Settled once the adapter is idle or Windows itself established an association
                    // (auto-connect). Neither leaves a request of ours pending.
                    const auto cleanup_until = Clock::now() + 3s;
                    bool settled{};
                    do {
                        const auto state = snapshot({}, false);
                        const auto adapter = std::find_if(state.interfaces.begin(), state.interfaces.end(), [&](const auto& item) {
                            return equal_id(item.id, b.interface_id);
                        });
                        if (adapter != state.interfaces.end() && (adapter->wlan_idle || adapter->associated)) { settled = true; break; }
                        std::this_thread::sleep_for(100ms);
                    } while (Clock::now() < cleanup_until);
                    if (!settled) return Error{"WlanDisconnect", ERROR_TIMEOUT, "Windows did not confirm Wi-Fi request cleanup", true};
                }
                if (stop.stop_requested()) return fail("WlanConnect cancelled", ERROR_CANCELLED);
                const auto hint = notice.last_attempt_reason.load(std::memory_order_acquire);
                return reported(hint, exact ? "Windows did not confirm the requested access point in time" :
                    "Windows did not confirm the requested network in time", WifiFailure::association_unconfirmed);
            }
            if (timing) timing->association = elapsed();
            if (progress) progress({WifiStage::obtaining_address, 0, 0, b.bssid});
            // Phase B: usable IPv4 and gateway, observed twice in a row. Never disconnect here.
            // A disconnection notice from the previous network may arrive late: only count those
            // after this association, and require repeated misses before declaring it lost.
            notice.disconnected.store(false, std::memory_order_release);
            std::string last_ip, last_gateway;
            unsigned misses{};
            const auto address_until = Clock::now() + budget.address;
            while (Clock::now() < address_until && !stop.stop_requested()) {
                Snapshot state;
                const auto* match = current_match(state);
                misses = match ? 0 : misses + 1;
                if (misses >= 3 && notice.disconnected.load(std::memory_order_acquire))
                    return reported(notice.last_attempt_reason.load(), "The association was lost while Windows was configuring the address", WifiFailure::radio);
                if (match && usable_ipv4(match->address) && usable_ipv4(match->gateway) && match->up) {
                    if (match->address == last_ip && match->gateway == last_gateway) {
                        if (timing) timing->usable = elapsed();
                        return {};
                    }
                    last_ip = match->address; last_gateway = match->gateway;
                } else { last_ip.clear(); last_gateway.clear(); }
                if (!settle(400ms, stop)) break;
            }
            if (stop.stop_requested()) return fail("WlanConnect cancelled", ERROR_CANCELLED);
            return Error{"DHCP", ERROR_TIMEOUT, "Associated; Windows has not yet reported a usable IPv4 address and gateway", false, WifiFailure::ip_unconfirmed};
        }
        return fail("WlanConnect interface", ERROR_NOT_FOUND);
    }
public:
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
        const auto same_link = [&] {
            const auto observed = snapshot(stop, false);
            return std::any_of(observed.interfaces.begin(), observed.interfaces.end(), [&](const auto& after) {
                return after.index == i.index && equal_id(after.id, i.id) && after.bssid == i.bssid &&
                    after.profile == i.profile && after.address == i.address && after.gateway == i.gateway && after.associated;
            });
        };
        const auto discarded = [] {
            Observation unavailable;
            unavailable.error = {"Interface consistency", ERROR_NOT_READY, "Association/address/gateway changed; observations discarded"};
            return unavailable;
        };
        if (!settle(std::chrono::milliseconds(scope.settle_ms), stop) || !same_link()) return discarded();
        for (unsigned n = 0; n < 3 && !scope.dns_name.empty() && !stop.stop_requested(); ++n)
            if (const auto latency = resolve(i, scope, source, target.sin_addr, stop)) o.dns_history.push_back(*latency);
        const Icmp ping;
        IN_ADDR gateway{};
        if (!stop.stop_requested() && InetPtonA(AF_INET, i.gateway.c_str(), &gateway) == 1)
            o.gateway_replied = echo(ping, source.sin_addr, gateway, 350).has_value();
        for (unsigned n = 0; n < scope.repetitions && !stop.stop_requested() && ping.value != INVALID_HANDLE_VALUE; ++n) {
            ++o.attempts;
            if (const auto response = echo(ping, source.sin_addr, target.sin_addr, (std::min)(scope.timeout_ms, 350u))) {
                ++o.replies; o.latency_history.push_back(static_cast<float>(*response));
            }
            if (!settle(60ms, stop)) break;
        }
        bool first = true;
        for (const auto size : scope.transfer_sizes()) {
            TransferSeries series; series.bytes = size;
            for (unsigned n = 0; n < scope.transfer_repetitions && !stop.stop_requested(); ++n) {
                if (!first && !settle(1050ms, stop)) break; // Match the supplied endpoint's one-request-per-second ceiling.
                first = false;
                if (!same_link()) return discarded();
                ++series.attempts;
                if (const auto rates = transfer(i, scope, size, source, target, stop)) {
                    series.up.push_back(rates->first); series.down.push_back(rates->second); o.bytes += size * 2;
                }
            }
            o.transfers.push_back(std::move(series));
        }
        if (stop.stop_requested() || !same_link()) return discarded();
        summarize_observation(o);
        if (!o.endpoint_verified || !o.sample_complete) o.error = {"CLP1", ERROR_TIMEOUT, "One or more controlled transfers did not complete within scope"};
        return o;
    }
};
}
Backend& platform() { static Windows backend; return backend; }
std::optional<Error> loopback_protocol(unsigned port) {
    if (!port || port > 65535) return fail("CLP1 loopback port", ERROR_INVALID_PARAMETER);
    const Winsock wsa;
    if (wsa.code) return fail("WSAStartup", static_cast<DWORD>(wsa.code));
    // Explicit test path only: both addresses are fixed to loopback; it cannot reach a selected network.
    sockaddr_in source{}, target{};
    source.sin_family = target.sin_family = AF_INET;
    source.sin_addr.S_un.S_addr = target.sin_addr.S_un.S_addr = htonl(INADDR_LOOPBACK);
    target.sin_port = htons(static_cast<u_short>(port));
    Scope scope; scope.timeout_ms = 2000;
    const Interface local;
    for (const auto size : {1024u, 16384u, 65536u}) {
        const auto result = transfer(local, scope, size, source, target, {});
        if (!result || result->first <= 0 || result->second <= 0) return fail("CLP1 fixed loopback exchange", ERROR_INVALID_DATA);
    }
    return {};
}
}
