#ifndef WIFI_DIRECT_COMMAND_HANDLER_H
#define WIFI_DIRECT_COMMAND_HANDLER_H

#include "wifi_manager.h"
#include "communication/tcp.h"
#include <iostream>
#include <string>
#include <vector>

namespace wifi_direct {

/// Format a list of networks into a readable table string.
std::string format_network_table(const std::vector<NetworkInfo>& networks);

/// Handle the "scan" command.
void cmd_scan(WiFiManager& manager, bool show_all);

/// Handle the "connect" command.
void cmd_connect(WiFiManager& manager, const std::string& ssid, const std::string& password);

/// Handle the "status" command.
void cmd_status(WiFiManager& manager);

/// Handle the "disconnect" command.
void cmd_disconnect(WiFiManager& manager);

/// Handle the "tcp_server" command.
void cmd_tcp_server(int port);

/// Print usage/help information.
void print_usage(const char* program_name);

/// Parse command-line arguments and dispatch to the appropriate handler.
int run(int argc, char* argv[]);

} // namespace wifi_direct

#endif // WIFI_DIRECT_COMMAND_HANDLER_H
