#ifndef WIFI_DIRECT_WIFI_MANAGER_H
#define WIFI_DIRECT_WIFI_MANAGER_H

#include "network_info.h"
#include <optional>
#include <string>
#include <vector>

namespace wifi_direct {

/// Manager for Wi-Fi Direct connections on macOS.
class WiFiManager {
public:
    static constexpr const char* WIFI_DIRECT_PREFIX = "DIRECT-";

    WiFiManager();

    /// Scan for available Wi-Fi networks.
    /// @param show_all If true, show all networks; otherwise only Wi-Fi Direct.
    std::vector<NetworkInfo> scan_networks(bool show_all = false);

    /// Connect to a Wi-Fi network.
    /// @param ssid The SSID of the network.
    /// @param password The password (empty string for open networks).
    /// @return true if connection was successful.
    bool connect(const std::string& ssid, const std::string& password = "");

    /// Disconnect from the current Wi-Fi network.
    bool disconnect();

    /// Get the SSID of the currently connected network.
    std::optional<std::string> get_current_network() const;

    /// Get the current Wi-Fi connection status.
    WiFiStatus get_status() const;

    /// Get the Wi-Fi interface name.
    const std::string& get_interface() const { return interface_; }

private:
    std::string interface_;

    /// Detect the Wi-Fi interface name (e.g. en1).
    static std::string detect_wifi_interface();

    /// Check if Wi-Fi is powered on.
    bool is_wifi_powered_on() const;

    /// Turn on Wi-Fi.
    bool power_on_wifi();

    /// Scan using a Swift CoreWLAN script (primary method).
    /// @return Networks found, or std::nullopt on failure.
    std::optional<std::vector<NetworkInfo>> scan_with_swift_corewlan();

    /// Display connection info after successful connection.
    void show_connection_info() const;

    /// Run a shell command and capture stdout/stderr.
    struct CmdResult {
        int exit_code;
        std::string stdout_str;
        std::string stderr_str;
    };
    static CmdResult run_command(const std::vector<std::string>& args, int timeout_sec = 30);
};

} // namespace wifi_direct

#endif // WIFI_DIRECT_WIFI_MANAGER_H
