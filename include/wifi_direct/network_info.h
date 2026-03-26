#ifndef WIFI_DIRECT_NETWORK_INFO_H
#define WIFI_DIRECT_NETWORK_INFO_H

#include <string>
#include <vector>

namespace wifi_direct {

/// Holds information about a scanned Wi-Fi network.
struct NetworkInfo {
    std::string ssid;
    std::string bssid;
    std::string rssi;      // Signal strength in dBm
    std::string channel;
    std::string security;

    /// Check if this is a Wi-Fi Direct network (SSID starts with "DIRECT-").
    bool is_wifi_direct() const {
        return ssid.rfind("DIRECT-", 0) == 0;
    }
};

/// Holds the current Wi-Fi connection status.
struct WiFiStatus {
    std::string interface;
    bool power_on = false;
    std::string ssid;
    std::string ip_address;
    std::string subnet_mask;
    std::string router;
};

} // namespace wifi_direct

#endif // WIFI_DIRECT_NETWORK_INFO_H
