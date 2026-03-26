#include "wifi_direct/wifi_manager.h"

#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <regex>
#include <sstream>
#include <thread>

#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif

namespace wifi_direct {

// ---------------------------------------------------------------------------
// Helper: run a command and capture output
// ---------------------------------------------------------------------------
WiFiManager::CmdResult WiFiManager::run_command(const std::vector<std::string>& args, int timeout_sec) {
    CmdResult result{-1, "", ""};
    if (args.empty()) return result;

    // Build argv
    std::vector<const char*> argv;
    for (auto& a : args) argv.push_back(a.c_str());
    argv.push_back(nullptr);

    int stdout_pipe[2], stderr_pipe[2];
    if (pipe(stdout_pipe) != 0 || pipe(stderr_pipe) != 0) return result;

    pid_t pid = fork();
    if (pid < 0) {
        return result;
    }

    if (pid == 0) {
        // Child
        close(stdout_pipe[0]);
        close(stderr_pipe[0]);
        dup2(stdout_pipe[1], STDOUT_FILENO);
        dup2(stderr_pipe[1], STDERR_FILENO);
        close(stdout_pipe[1]);
        close(stderr_pipe[1]);
        execvp(argv[0], const_cast<char* const*>(argv.data()));
        _exit(127);
    }

    // Parent
    close(stdout_pipe[1]);
    close(stderr_pipe[1]);

    // Read stdout and stderr
    auto read_fd = [](int fd) -> std::string {
        std::string data;
        char buf[4096];
        ssize_t n;
        while ((n = read(fd, buf, sizeof(buf))) > 0) {
            data.append(buf, n);
        }
        close(fd);
        return data;
    };

    // Use threads to read both pipes concurrently
    std::string out_data, err_data;
    std::thread out_thread([&]() { out_data = read_fd(stdout_pipe[0]); });
    std::thread err_thread([&]() { err_data = read_fd(stderr_pipe[0]); });

    // Wait with timeout
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(timeout_sec);
    int status = 0;
    bool timed_out = false;

    while (true) {
        int ret = waitpid(pid, &status, WNOHANG);
        if (ret == pid) break;
        if (ret < 0) break;
        if (std::chrono::steady_clock::now() >= deadline) {
            kill(pid, SIGTERM);
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            kill(pid, SIGKILL);
            waitpid(pid, &status, 0);
            timed_out = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    out_thread.join();
    err_thread.join();

    result.stdout_str = std::move(out_data);
    result.stderr_str = std::move(err_data);
    result.exit_code = timed_out ? -1 : (WIFEXITED(status) ? WEXITSTATUS(status) : -1);
    return result;
}

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------
WiFiManager::WiFiManager()
    : interface_(detect_wifi_interface()) {}

// ---------------------------------------------------------------------------
// Detect Wi-Fi interface
// ---------------------------------------------------------------------------
std::string WiFiManager::detect_wifi_interface() {
    auto res = run_command({"networksetup", "-listallhardwareports"});
    if (res.exit_code != 0) return "en0";

    std::istringstream iss(res.stdout_str);
    std::string line;
    bool found_wifi = false;

    while (std::getline(iss, line)) {
        // Match "Hardware Port: Wi-Fi" or "Hardware Port: AirPort"
        if (std::regex_search(line, std::regex(R"(Hardware Port:\s*(Wi-Fi|AirPort))"))) {
            found_wifi = true;
            continue;
        }
        if (found_wifi) {
            // Look for "Device: enX"
            std::smatch m;
            if (std::regex_search(line, m, std::regex(R"(^\s*Device:\s*(\S+))"))) {
                return m[1].str();
            }
        }
    }
    return "en0";
}

// ---------------------------------------------------------------------------
// Power management
// ---------------------------------------------------------------------------
bool WiFiManager::is_wifi_powered_on() const {
    auto res = run_command({"networksetup", "-getairportpower", interface_});
    return res.exit_code == 0 && res.stdout_str.find("On") != std::string::npos;
}

bool WiFiManager::power_on_wifi() {
    auto res = run_command({"networksetup", "-setairportpower", interface_, "on"});
    if (res.exit_code != 0) {
        std::cerr << "[ERROR] Failed to turn on Wi-Fi." << std::endl;
        return false;
    }
    std::this_thread::sleep_for(std::chrono::seconds(2));
    return true;
}

// ---------------------------------------------------------------------------
// Scan networks
// ---------------------------------------------------------------------------
std::vector<NetworkInfo> WiFiManager::scan_networks(bool show_all) {
    if (!is_wifi_powered_on()) {
        std::cout << "[INFO] Wi-Fi is off. Turning on Wi-Fi..." << std::endl;
        if (!power_on_wifi()) return {};
    }

    std::cout << "[INFO] Scanning Wi-Fi networks on interface " << interface_ << "..." << std::endl;

    // Primary: Swift CoreWLAN
    auto networks = scan_with_swift_corewlan();

    // Fallback: system_profiler
    if (!networks.has_value()) {
        std::cout << "[INFO] Falling back to system_profiler scan..." << std::endl;
        networks = scan_with_system_profiler();
    }

    auto& nets = networks.value();
    if (!show_all) {
        std::vector<NetworkInfo> filtered;
        for (auto& n : nets) {
            if (n.is_wifi_direct()) filtered.push_back(std::move(n));
        }
        return filtered;
    }
    return nets;
}

// ---------------------------------------------------------------------------
// Swift CoreWLAN scan
// ---------------------------------------------------------------------------
std::optional<std::vector<NetworkInfo>> WiFiManager::scan_with_swift_corewlan() {
    // Locate the bundled Swift script
    // Try several possible locations relative to the executable
    std::vector<std::string> search_paths;

    // 1. Next to the executable: ../scripts/scan_wifi.swift
    {
        char exe_path[1024];
        uint32_t size = sizeof(exe_path);
        if (_NSGetExecutablePath(exe_path, &size) == 0) {
            std::filesystem::path p(exe_path);
            search_paths.push_back((p.parent_path().parent_path() / "scripts" / "scan_wifi.swift").string());
            search_paths.push_back((p.parent_path() / "scripts" / "scan_wifi.swift").string());
            search_paths.push_back((p.parent_path() / "scan_wifi.swift").string());
        }
    }
    // 2. Current working directory
    search_paths.push_back("scripts/scan_wifi.swift");
    search_paths.push_back("scan_wifi.swift");

    std::string swift_script_path;
    for (auto& sp : search_paths) {
        if (std::filesystem::exists(sp)) {
            swift_script_path = sp;
            break;
        }
    }

    if (swift_script_path.empty()) {
        // Fall back to inline Swift code via temp file
        char tmp_path[] = "/tmp/wifi_scan_XXXXXX.swift";
        // Use mkstemp-like approach
        std::string tmp_file = "/tmp/wifi_scan_" + std::to_string(getpid()) + ".swift";
        std::ofstream ofs(tmp_file);
        if (!ofs) {
            std::cerr << "[WARN] Cannot create temp Swift script." << std::endl;
            return std::nullopt;
        }
        ofs << R"SWIFT(
import Foundation
import CoreWLAN

guard let iface = CWWiFiClient.shared().interface() else {
    fputs("ERROR: No Wi-Fi interface found\n", stderr)
    exit(1)
}

do {
    let networks = try iface.scanForNetworks(withName: nil)
    for net in networks {
        let ssid = net.ssid ?? ""
        let bssid = net.bssid ?? ""
        let rssi = net.rssiValue
        let channel = net.wlanChannel?.channelNumber ?? 0
        var security = "Unknown"
        if net.supportsSecurity(.wpa3Personal) || net.supportsSecurity(.wpa3Enterprise) {
            security = "WPA3"
        } else if net.supportsSecurity(.wpa2Personal) || net.supportsSecurity(.wpa2Enterprise) {
            security = "WPA2"
        } else if net.supportsSecurity(.wpaPersonal) || net.supportsSecurity(.wpaEnterprise) {
            security = "WPA"
        } else if net.supportsSecurity(.dynamicWEP) {
            security = "WEP"
        } else if net.supportsSecurity(.none) {
            security = "Open"
        }
        print("\(ssid)|\(bssid)|\(rssi)|\(channel)|\(security)")
    }
} catch {
    fputs("ERROR: \(error.localizedDescription)\n", stderr)
    exit(1)
}
)SWIFT";
        ofs.close();
        swift_script_path = tmp_file;
    }

    auto res = run_command({"swift", swift_script_path}, 30);

    // Clean up temp file if we created one
    if (swift_script_path.find("/tmp/wifi_scan_") == 0) {
        std::filesystem::remove(swift_script_path);
    }

    if (res.exit_code != 0) {
        std::cerr << "[WARN] Swift CoreWLAN scan failed: " << res.stderr_str << std::endl;
        return std::nullopt;
    }

    std::vector<NetworkInfo> networks;
    std::istringstream iss(res.stdout_str);
    std::string line;
    while (std::getline(iss, line)) {
        if (line.empty() || line.find('|') == std::string::npos) continue;

        // Parse pipe-delimited: ssid|bssid|rssi|channel|security
        std::vector<std::string> parts;
        std::istringstream ls(line);
        std::string part;
        while (std::getline(ls, part, '|')) {
            parts.push_back(part);
        }
        if (parts.size() >= 5) {
            networks.push_back({parts[0], parts[1], parts[2], parts[3], parts[4]});
        }
    }
    return networks;
}

// ---------------------------------------------------------------------------
// system_profiler scan
// ---------------------------------------------------------------------------
std::vector<NetworkInfo> WiFiManager::scan_with_system_profiler() {
    auto res = run_command({"system_profiler", "SPAirPortDataType"}, 30);
    if (res.exit_code != 0) {
        std::cerr << "[WARN] system_profiler scan failed." << std::endl;
        return {};
    }
    return parse_system_profiler_output(res.stdout_str);
}

std::vector<NetworkInfo> WiFiManager::parse_system_profiler_output(const std::string& output) {
    std::vector<NetworkInfo> networks;
    std::istringstream iss(output);
    std::string line;

    enum class Section { None, Current, Other };
    Section section = Section::None;

    std::string current_ssid;
    NetworkInfo current_net;
    bool has_current = false;

    auto flush_network = [&]() {
        if (has_current && !current_ssid.empty()) {
            current_net.ssid = current_ssid;
            networks.push_back(current_net);
        }
        current_ssid.clear();
        current_net = {"", "N/A", "N/A", "N/A", "N/A"};
        has_current = false;
    };

    while (std::getline(iss, line)) {
        // Trim
        std::string stripped = line;
        size_t start = stripped.find_first_not_of(" \t");
        if (start != std::string::npos) stripped = stripped.substr(start);
        else stripped.clear();

        if (stripped.find("Current Network Information:") != std::string::npos) {
            flush_network();
            section = Section::Current;
            continue;
        }
        if (stripped.find("Other Local Wi-Fi Networks:") != std::string::npos) {
            flush_network();
            section = Section::Other;
            continue;
        }

        // Detect section end: unindented non-empty line
        if (!stripped.empty() && !line.empty() && line[0] != ' ' && line[0] != '\t') {
            flush_network();
            section = Section::None;
            continue;
        }

        if (section == Section::Current || section == Section::Other) {
            // SSID line: ends with ":" and no other ":" in the name
            if (!stripped.empty() && stripped.back() == ':') {
                std::string name = stripped.substr(0, stripped.size() - 1);
                if (name.find(':') == std::string::npos) {
                    flush_network();
                    current_ssid = name;
                    current_net = {"", "N/A", "N/A", "N/A", "N/A"};
                    has_current = true;
                    continue;
                }
            }
            if (has_current) {
                parse_network_property(stripped, current_net);
            }
        }
    }
    flush_network();
    return networks;
}

void WiFiManager::parse_network_property(const std::string& line, NetworkInfo& net) {
    std::smatch m;
    if (line.rfind("Channel:", 0) == 0) {
        // e.g. "Channel: 44 (5GHz, 40MHz)"
        if (std::regex_search(line, m, std::regex(R"(Channel:\s*(\d+))"))) {
            net.channel = m[1].str();
        }
    } else if (line.rfind("Security:", 0) == 0) {
        size_t pos = line.find(':');
        if (pos != std::string::npos) {
            std::string val = line.substr(pos + 1);
            size_t s = val.find_first_not_of(' ');
            net.security = (s != std::string::npos) ? val.substr(s) : val;
        }
    } else if (line.rfind("Signal / Noise:", 0) == 0) {
        // e.g. "Signal / Noise: -62 dBm / -98 dBm"
        if (std::regex_search(line, m, std::regex(R"(Signal / Noise:\s*(-?\d+))"))) {
            net.rssi = m[1].str();
        }
    }
}

// ---------------------------------------------------------------------------
// Connect
// ---------------------------------------------------------------------------
bool WiFiManager::connect(const std::string& ssid, const std::string& password) {
    if (!is_wifi_powered_on()) {
        std::cout << "[INFO] Wi-Fi is off. Turning on Wi-Fi..." << std::endl;
        if (!power_on_wifi()) return false;
    }

    std::cout << "[INFO] Connecting to '" << ssid << "'..." << std::endl;

    std::vector<std::string> cmd = {"networksetup", "-setairportnetwork", interface_, ssid};
    if (!password.empty()) {
        cmd.push_back(password);
    }

    auto res = run_command(cmd, 30);

    if (res.exit_code == 0 && res.stderr_str.empty()) {
        std::this_thread::sleep_for(std::chrono::seconds(3));
        auto current = get_current_network();
        if (current.has_value() && current.value() == ssid) {
            std::cout << "[OK] Successfully connected to '" << ssid << "'" << std::endl;
            show_connection_info();
            return true;
        } else {
            std::cout << "[WARN] Command succeeded but connection to '" << ssid << "' could not be verified." << std::endl;
            std::cout << "[INFO] Current network: " << (current.has_value() ? current.value() : "None") << std::endl;
            return false;
        }
    } else {
        std::string error_msg = res.stderr_str.empty() ? res.stdout_str : res.stderr_str;
        if (error_msg.find("Could not find network") != std::string::npos) {
            std::cerr << "[ERROR] Network '" << ssid << "' not found. Make sure the Android device's Wi-Fi Direct hotspot is active." << std::endl;
        } else if (error_msg.find("password") != std::string::npos || error_msg.find("Password") != std::string::npos) {
            std::cerr << "[ERROR] Incorrect password or password required for '" << ssid << "'." << std::endl;
        } else {
            std::cerr << "[ERROR] Failed to connect: " << error_msg << std::endl;
        }
        return false;
    }
}

// ---------------------------------------------------------------------------
// Disconnect
// ---------------------------------------------------------------------------
bool WiFiManager::disconnect() {
    auto res1 = run_command({"networksetup", "-setairportpower", interface_, "off"}, 5);
    std::this_thread::sleep_for(std::chrono::seconds(1));
    auto res2 = run_command({"networksetup", "-setairportpower", interface_, "on"}, 5);

    if (res1.exit_code == 0 && res2.exit_code == 0) {
        std::cout << "[OK] Disconnected from Wi-Fi network." << std::endl;
        return true;
    }
    std::cerr << "[ERROR] Failed to disconnect." << std::endl;
    return false;
}

// ---------------------------------------------------------------------------
// Get current network
// ---------------------------------------------------------------------------
std::optional<std::string> WiFiManager::get_current_network() const {
    auto res = run_command({"networksetup", "-getairportnetwork", interface_});
    if (res.exit_code != 0) return std::nullopt;

    std::smatch m;
    if (std::regex_search(res.stdout_str, m,
            std::regex(R"((?:Current Wi-Fi Network|Current AirPort Network):\s*(.+))"))) {
        std::string ssid = m[1].str();
        // Trim trailing whitespace/newline
        while (!ssid.empty() && (ssid.back() == '\n' || ssid.back() == '\r' || ssid.back() == ' ')) {
            ssid.pop_back();
        }
        return ssid;
    }
    return std::nullopt;
}

// ---------------------------------------------------------------------------
// Get status
// ---------------------------------------------------------------------------
WiFiStatus WiFiManager::get_status() const {
    WiFiStatus status;
    status.interface = interface_;
    status.power_on = is_wifi_powered_on();

    if (status.power_on) {
        auto ssid = get_current_network();
        if (ssid.has_value()) status.ssid = ssid.value();

        auto ip_res = run_command({"ipconfig", "getifaddr", interface_}, 5);
        if (ip_res.exit_code == 0) {
            status.ip_address = ip_res.stdout_str;
            // Trim
            while (!status.ip_address.empty() &&
                   (status.ip_address.back() == '\n' || status.ip_address.back() == '\r')) {
                status.ip_address.pop_back();
            }
        }

        auto info_res = run_command({"networksetup", "-getinfo", "Wi-Fi"}, 5);
        if (info_res.exit_code == 0) {
            std::istringstream iss(info_res.stdout_str);
            std::string line;
            while (std::getline(iss, line)) {
                if (line.rfind("Subnet mask:", 0) == 0) {
                    status.subnet_mask = line.substr(line.find(':') + 1);
                    size_t s = status.subnet_mask.find_first_not_of(' ');
                    if (s != std::string::npos) status.subnet_mask = status.subnet_mask.substr(s);
                } else if (line.rfind("Router:", 0) == 0) {
                    status.router = line.substr(line.find(':') + 1);
                    size_t s = status.router.find_first_not_of(' ');
                    if (s != std::string::npos) status.router = status.router.substr(s);
                }
            }
        }
    }
    return status;
}

// ---------------------------------------------------------------------------
// Show connection info
// ---------------------------------------------------------------------------
void WiFiManager::show_connection_info() const {
    auto status = get_status();
    if (!status.ip_address.empty())
        std::cout << "  IP Address : " << status.ip_address << std::endl;
    if (!status.subnet_mask.empty())
        std::cout << "  Subnet Mask: " << status.subnet_mask << std::endl;
    if (!status.router.empty())
        std::cout << "  Router     : " << status.router << std::endl;
}

} // namespace wifi_direct
