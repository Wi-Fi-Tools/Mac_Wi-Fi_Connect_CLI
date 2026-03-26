#include "wifi_direct/command_handler.h"
#include "communication/tcp.h"

#include <algorithm>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <termios.h>
#include <unistd.h>

namespace wifi_direct {

// ---------------------------------------------------------------------------
// Format network table
// ---------------------------------------------------------------------------
std::string format_network_table(const std::vector<NetworkInfo>& networks) {
    if (networks.empty()) {
        return "  No networks found.";
    }

    // Calculate max SSID width
    size_t ssid_w = 4; // minimum "SSID"
    for (auto& n : networks) {
        ssid_w = std::max(ssid_w, n.ssid.size());
    }
    ssid_w += 2;

    std::ostringstream oss;

    // Header
    oss << "  " << std::left
        << std::setw(4) << "#"
        << std::setw(static_cast<int>(ssid_w)) << "SSID"
        << std::setw(20) << "BSSID"
        << std::setw(8) << "RSSI"
        << std::setw(6) << "CH"
        << "SECURITY" << "\n";

    // Separator
    oss << "  " << std::string(4 + ssid_w + 20 + 8 + 6 + 16, '-') << "\n";

    // Rows
    for (size_t i = 0; i < networks.size(); ++i) {
        auto& n = networks[i];
        oss << "  " << std::left
            << std::setw(4) << (i + 1)
            << std::setw(static_cast<int>(ssid_w)) << n.ssid
            << std::setw(20) << (n.bssid.empty() ? "N/A" : n.bssid)
            << std::setw(8) << (n.rssi.empty() ? "N/A" : n.rssi)
            << std::setw(6) << (n.channel.empty() ? "N/A" : n.channel)
            << (n.security.empty() ? "N/A" : n.security);
        if (i + 1 < networks.size()) oss << "\n";
    }

    return oss.str();
}

// ---------------------------------------------------------------------------
// Read password without echo
// ---------------------------------------------------------------------------
static std::string read_password(const std::string& prompt) {
    std::cout << prompt;
    std::cout.flush();

    struct termios old_term, new_term;
    tcgetattr(STDIN_FILENO, &old_term);
    new_term = old_term;
    new_term.c_lflag &= ~ECHO;
    tcsetattr(STDIN_FILENO, TCSANOW, &new_term);

    std::string password;
    std::getline(std::cin, password);

    tcsetattr(STDIN_FILENO, TCSANOW, &old_term);
    std::cout << std::endl;
    return password;
}

// ---------------------------------------------------------------------------
// cmd_scan
// ---------------------------------------------------------------------------
void cmd_scan(WiFiManager& manager, bool show_all) {
    auto networks = manager.scan_networks(show_all);

    if (show_all) {
        std::cout << "\n[RESULT] Found " << networks.size() << " Wi-Fi network(s):\n\n";
    } else {
        std::cout << "\n[RESULT] Found " << networks.size() << " Wi-Fi Direct network(s):\n\n";
    }

    std::cout << format_network_table(networks) << std::endl;

    if (networks.empty() && !show_all) {
        std::cout << "\n[TIP] No Wi-Fi Direct networks found.\n"
                  << "  - Make sure the Android device has Wi-Fi Direct hotspot enabled.\n"
                  << "  - Android Wi-Fi Direct SSIDs typically start with 'DIRECT-'.\n"
                  << "  - Use '--all' flag to see all available networks.\n";
    }
    std::cout << std::endl;
}

// ---------------------------------------------------------------------------
// cmd_connect
// ---------------------------------------------------------------------------
void cmd_connect(WiFiManager& manager, const std::string& ssid_arg, const std::string& password_arg) {
    std::string ssid = ssid_arg;
    std::string password = password_arg;

    if (ssid.empty()) {
        // Interactive mode: scan and let user choose
        std::cout << "\n[INFO] Scanning for Wi-Fi Direct networks...\n" << std::endl;
        auto networks = manager.scan_networks(false);

        if (networks.empty()) {
            std::cout << "[INFO] No Wi-Fi Direct networks found. Scanning all networks...\n" << std::endl;
            networks = manager.scan_networks(true);
        }

        if (networks.empty()) {
            std::cerr << "[ERROR] No Wi-Fi networks found. Please check that Wi-Fi is enabled." << std::endl;
            return;
        }

        std::cout << format_network_table(networks) << "\n" << std::endl;

        while (true) {
            std::cout << "Enter the number of the network to connect to (or 'q' to quit): ";
            std::cout.flush();
            std::string choice;
            if (!std::getline(std::cin, choice)) {
                std::cout << "\n[INFO] Cancelled." << std::endl;
                return;
            }
            // Trim
            while (!choice.empty() && (choice.back() == ' ' || choice.back() == '\n')) choice.pop_back();

            if (choice == "q" || choice == "Q") {
                std::cout << "[INFO] Cancelled." << std::endl;
                return;
            }

            try {
                int idx = std::stoi(choice) - 1;
                if (idx >= 0 && idx < static_cast<int>(networks.size())) {
                    ssid = networks[idx].ssid;
                    break;
                }
                std::cout << "[WARN] Please enter a number between 1 and " << networks.size() << "." << std::endl;
            } catch (...) {
                std::cout << "[WARN] Please enter a valid number." << std::endl;
            }
        }
    }

    if (password.empty()) {
        password = read_password("Enter password for '" + ssid + "' (press Enter if open): ");
    }

    manager.connect(ssid, password);
}

// ---------------------------------------------------------------------------
// cmd_status
// ---------------------------------------------------------------------------
void cmd_status(WiFiManager& manager) {
    auto status = manager.get_status();

    std::cout << "\n[Wi-Fi Status]\n"
              << "  Interface  : " << status.interface << "\n"
              << "  Power      : " << (status.power_on ? "On" : "Off") << "\n";

    if (status.power_on) {
        std::string display_ssid = status.ssid.empty() ? "Not connected" : status.ssid;
        bool is_direct = !status.ssid.empty() &&
                         status.ssid.rfind(WiFiManager::WIFI_DIRECT_PREFIX, 0) == 0;
        std::cout << "  Network    : " << display_ssid
                  << (is_direct ? " (Wi-Fi Direct)" : "") << "\n"
                  << "  IP Address : " << (status.ip_address.empty() ? "N/A" : status.ip_address) << "\n"
                  << "  Subnet Mask: " << (status.subnet_mask.empty() ? "N/A" : status.subnet_mask) << "\n"
                  << "  Router     : " << (status.router.empty() ? "N/A" : status.router) << "\n";
    }
    std::cout << std::endl;
}

// ---------------------------------------------------------------------------
// cmd_disconnect
// ---------------------------------------------------------------------------
void cmd_disconnect(WiFiManager& manager) {
    auto current = manager.get_current_network();
    if (current.has_value()) {
        std::cout << "[INFO] Disconnecting from '" << current.value() << "'..." << std::endl;
        manager.disconnect();
    } else {
        std::cout << "[INFO] Not currently connected to any Wi-Fi network." << std::endl;
    }
}

// ---------------------------------------------------------------------------
// cmd_tcp_server
// ---------------------------------------------------------------------------
void cmd_tcp_server(int port) {
    std::cout << "[INFO] Starting TCP server on port " << port << "..." << std::endl;
    TcpServer server(port);
    if (!server.Listen()) {
        std::cout << "[ERROR] Failed to start TCP server." << std::endl;
        return;
    }
    if (!server.Accept()) {
        std::cout << "[ERROR] Failed to accept connection." << std::endl;
        return;
    }
    std::string message;
    server.Receive(message);
    std::cout << "[INFO] Received message: " << message << std::endl;
    server.Send("Hello from Mac!");
    server.Close();
}

// ---------------------------------------------------------------------------
// Usage
// ---------------------------------------------------------------------------
void print_usage(const char* program_name) {
    std::cout << "Mac CLI tool for connecting to Android Wi-Fi Direct hotspots.\n\n"
              << "Usage:\n"
              << "  " << program_name << " scan [--all]                    Scan for Wi-Fi networks\n"
              << "  " << program_name << " connect                         Interactive connect\n"
              << "  " << program_name << " connect -s <SSID> -p <password> Connect directly\n"
              << "  " << program_name << " status                          Show Wi-Fi status\n"
              << "  " << program_name << " disconnect                      Disconnect\n\n"
              << "  " << program_name << " tcp_server -p <port>            Start a TCP server\n"
              << "Options:\n"
              << "  --all, -a    Show all Wi-Fi networks (not just Wi-Fi Direct)\n"
              << "  -s <SSID>    SSID of the network to connect to\n"
              << "  -p <pass>    Password for the network\n"
              << "  -p <port>    Port number for the TCP server\n\n"
              << "Notes:\n"
              << "  - Android Wi-Fi Direct hotspot SSIDs typically start with 'DIRECT-'.\n"
              << "  - Some operations may require administrator privileges (sudo).\n"
              << "  - Make sure the Android device's Wi-Fi Direct hotspot is active before connecting.\n";
}

// ---------------------------------------------------------------------------
// Main entry: parse args and dispatch
// ---------------------------------------------------------------------------
int run(int argc, char* argv[]) {
    if (argc < 2) {
        print_usage(argv[0]);
        return 0;
    }

    std::string command = argv[1];
    WiFiManager manager;

    std::cout << "Interface: " << manager.get_interface() << std::endl;

    if (command == "scan") {
        bool show_all = false;
        for (int i = 2; i < argc; ++i) {
            std::string arg = argv[i];
            if (arg == "--all" || arg == "-a") show_all = true;
        }
        cmd_scan(manager, show_all);

    } else if (command == "connect") {
        std::string ssid, password;
        for (int i = 2; i < argc; ++i) {
            std::string arg = argv[i];
            if ((arg == "-s" || arg == "--ssid") && i + 1 < argc) {
                ssid = argv[++i];
            } else if ((arg == "-p" || arg == "--password") && i + 1 < argc) {
                password = argv[++i];
            }
        }
        cmd_connect(manager, ssid, password);

    } else if (command == "status") {
        cmd_status(manager);

    } else if (command == "disconnect") {
        cmd_disconnect(manager);

    } else if (command == "tcp_server") {
        int port = 8080;
        for (int i = 2; i < argc; ++i) {
            std::string arg = argv[i];
            if ((arg == "-p" || arg == "--port") && i + 1 < argc) {
                port = std::stoi(argv[++i]);
            }
        }
        cmd_tcp_server(port);
    } else if (command == "--help" || command == "-h") {
        print_usage(argv[0]);

    } else {
        std::cerr << "Unknown command: " << command << "\n\n";
        print_usage(argv[0]);
        return 1;
    }

    return 0;
}

} // namespace wifi_direct
