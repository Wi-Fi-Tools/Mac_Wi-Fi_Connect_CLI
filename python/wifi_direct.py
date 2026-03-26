#!/usr/bin/env python3
"""
Mac CLI tool for connecting to Android Wi-Fi Direct hotspots.

Usage:
    python3 wifi_direct.py scan          # Scan for Wi-Fi Direct networks
    python3 wifi_direct.py connect       # Interactive connect to a Wi-Fi Direct network
    python3 wifi_direct.py connect -s <SSID> -p <password>  # Connect directly
    python3 wifi_direct.py status        # Show current Wi-Fi connection status
    python3 wifi_direct.py disconnect    # Disconnect from current Wi-Fi network
"""

import subprocess
import sys
import argparse
import os
import re
import time
import tempfile
import getpass
from typing import Optional


class WiFiDirectManager:
    """Manager for Wi-Fi Direct connections on macOS."""

    WIFI_DIRECT_PREFIX = "DIRECT-"

    def __init__(self):
        self.interface = self._get_wifi_interface()

    def _get_wifi_interface(self) -> str:
        """Get the default Wi-Fi interface name."""
        try:
            result = subprocess.run(
                ["networksetup", "-listallhardwareports"],
                capture_output=True, text=True, check=True
            )
            lines = result.stdout.split("\n")
            for i, line in enumerate(lines):
                # Match "Hardware Port: Wi-Fi" or "Hardware Port: AirPort"
                if re.search(r"Hardware Port:\s*(Wi-Fi|AirPort)", line):
                    for j in range(i + 1, min(i + 3, len(lines))):
                        if lines[j].strip().startswith("Device:"):
                            return lines[j].split(":", 1)[1].strip()
        except subprocess.CalledProcessError:
            pass
        # Default fallback
        return "en0"

    def _is_wifi_powered_on(self) -> bool:
        """Check if Wi-Fi is powered on."""
        try:
            result = subprocess.run(
                ["networksetup", "-getairportpower", self.interface],
                capture_output=True, text=True, check=True
            )
            return "On" in result.stdout
        except subprocess.CalledProcessError:
            return False

    def _power_on_wifi(self) -> bool:
        """Turn on Wi-Fi."""
        try:
            subprocess.run(
                ["networksetup", "-setairportpower", self.interface, "on"],
                capture_output=True, text=True, check=True
            )
            time.sleep(2)  # Wait for Wi-Fi to initialize
            return True
        except subprocess.CalledProcessError as e:
            print(f"[ERROR] Failed to turn on Wi-Fi: {e}")
            return False

    def scan_networks(self, show_all: bool = False) -> list[dict]:
        """
        Scan for available Wi-Fi networks.

        Args:
            show_all: If True, show all networks. If False, only show Wi-Fi Direct networks.

        Returns:
            List of network dictionaries with SSID, BSSID, RSSI, channel, and security info.
        """
        if not self._is_wifi_powered_on():
            print("[INFO] Wi-Fi is off. Turning on Wi-Fi...")
            if not self._power_on_wifi():
                return []

        print(f"[INFO] Scanning Wi-Fi networks on interface {self.interface}...")

        # Primary: use Swift CoreWLAN script (most reliable on modern macOS)
        networks = self._scan_with_swift_corewlan()

        # Fallback: use system_profiler
        if networks is None:
            print("[INFO] Falling back to system_profiler scan...")
            networks = self._scan_with_system_profiler()

        if not show_all:
            networks = [n for n in networks if n["ssid"].startswith(self.WIFI_DIRECT_PREFIX)]

        return networks

    def _scan_with_swift_corewlan(self) -> Optional[list[dict]]:
        """Scan using a Swift script that calls CoreWLAN directly."""
        swift_code = '''
import Foundation
import CoreWLAN

guard let iface = CWWiFiClient.shared().interface() else {
    fputs("ERROR: No Wi-Fi interface found\\n", stderr)
    exit(1)
}

do {
    let networks = try iface.scanForNetworks(withName: nil)
    for net in networks {
        let ssid = net.ssid ?? ""
        let bssid = net.bssid ?? ""
        let rssi = net.rssiValue
        let channel = net.wlanChannel?.channelNumber ?? 0
        // Determine security type
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
        print("\\(ssid)|\\(bssid)|\\(rssi)|\\(channel)|\\(security)")
    }
} catch {
    fputs("ERROR: \\(error.localizedDescription)\\n", stderr)
    exit(1)
}
'''
        try:
            # Write Swift script to a temp file
            with tempfile.NamedTemporaryFile(mode='w', suffix='.swift', delete=False) as f:
                f.write(swift_code)
                swift_path = f.name

            try:
                result = subprocess.run(
                    ["swift", swift_path],
                    capture_output=True, text=True, timeout=30
                )
            finally:
                os.unlink(swift_path)

            if result.returncode != 0:
                print(f"[WARN] Swift CoreWLAN scan failed: {result.stderr.strip()}")
                return None

            networks = []
            for line in result.stdout.strip().split("\n"):
                if not line or "|" not in line:
                    continue
                parts = line.split("|")
                if len(parts) >= 5:
                    networks.append({
                        "ssid": parts[0],
                        "bssid": parts[1],
                        "rssi": parts[2],
                        "channel": parts[3],
                        "security": parts[4],
                    })

            return networks

        except FileNotFoundError:
            print("[WARN] Swift not found. Cannot use CoreWLAN scan.")
            return None
        except subprocess.TimeoutExpired:
            print("[WARN] Swift CoreWLAN scan timed out.")
            return None
        except Exception as e:
            print(f"[WARN] Swift CoreWLAN scan error: {e}")
            return None

    def _scan_with_system_profiler(self) -> list[dict]:
        """Scan using system_profiler SPAirPortDataType."""
        try:
            result = subprocess.run(
                ["system_profiler", "SPAirPortDataType"],
                capture_output=True, text=True, timeout=30
            )
            if result.returncode != 0:
                print("[WARN] system_profiler scan failed.")
                return []

            return self._parse_system_profiler_output(result.stdout)

        except subprocess.TimeoutExpired:
            print("[WARN] system_profiler scan timed out.")
            return []
        except Exception as e:
            print(f"[WARN] system_profiler scan error: {e}")
            return []

    def _parse_system_profiler_output(self, output: str) -> list[dict]:
        """Parse the output of system_profiler SPAirPortDataType."""
        networks = []
        lines = output.split("\n")

        # Parse "Current Network Information" section for the connected network
        current_ssid = None
        in_current = False
        current_net = {}

        # Parse "Other Local Wi-Fi Networks" section for nearby networks
        in_other = False
        current_other_ssid = None
        current_other_net = {}

        for i, line in enumerate(lines):
            stripped = line.strip()

            if "Current Network Information:" in stripped:
                in_current = True
                in_other = False
                continue

            if "Other Local Wi-Fi Networks:" in stripped:
                # Save current network if we have one
                if current_ssid and current_net:
                    current_net["ssid"] = current_ssid
                    networks.append(current_net)
                in_current = False
                in_other = True
                continue

            # Detect section end (new top-level section like "awdl0:" or unindented line)
            if stripped and not line.startswith(" ") and not line.startswith("\t"):
                if in_other and current_other_ssid and current_other_net:
                    current_other_net["ssid"] = current_other_ssid
                    networks.append(current_other_net)
                in_current = False
                in_other = False
                continue

            if in_current:
                # SSID line ends with ":" and is indented
                if stripped.endswith(":") and ":" not in stripped[:-1]:
                    if current_ssid and current_net:
                        current_net["ssid"] = current_ssid
                        networks.append(current_net)
                    current_ssid = stripped[:-1]
                    current_net = {"bssid": "N/A", "rssi": "N/A", "channel": "N/A", "security": "N/A"}
                elif current_ssid:
                    self._parse_network_property(stripped, current_net)

            if in_other:
                # SSID line ends with ":" and is indented
                if stripped.endswith(":") and ":" not in stripped[:-1]:
                    if current_other_ssid and current_other_net:
                        current_other_net["ssid"] = current_other_ssid
                        networks.append(current_other_net)
                    current_other_ssid = stripped[:-1]
                    current_other_net = {"bssid": "N/A", "rssi": "N/A", "channel": "N/A", "security": "N/A"}
                elif current_other_ssid:
                    self._parse_network_property(stripped, current_other_net)

        # Don't forget the last network being parsed
        if in_current and current_ssid and current_net:
            current_net["ssid"] = current_ssid
            networks.append(current_net)
        if in_other and current_other_ssid and current_other_net:
            current_other_net["ssid"] = current_other_ssid
            networks.append(current_other_net)

        return networks

    @staticmethod
    def _parse_network_property(line: str, net: dict):
        """Parse a single property line from system_profiler output into a network dict."""
        if line.startswith("Channel:"):
            # e.g. "Channel: 44 (5GHz, 40MHz)"
            ch_match = re.match(r"Channel:\s*(\d+)", line)
            if ch_match:
                net["channel"] = ch_match.group(1)
        elif line.startswith("Security:"):
            net["security"] = line.split(":", 1)[1].strip()
        elif line.startswith("Signal / Noise:"):
            # e.g. "Signal / Noise: -62 dBm / -98 dBm"
            sig_match = re.match(r"Signal / Noise:\s*(-?\d+)", line)
            if sig_match:
                net["rssi"] = sig_match.group(1)

    def connect(self, ssid: str, password: Optional[str] = None) -> bool:
        """
        Connect to a Wi-Fi network.

        Args:
            ssid: The SSID of the network to connect to.
            password: The password for the network (if required).

        Returns:
            True if connection was successful, False otherwise.
        """
        if not self._is_wifi_powered_on():
            print("[INFO] Wi-Fi is off. Turning on Wi-Fi...")
            if not self._power_on_wifi():
                return False

        print(f"[INFO] Connecting to '{ssid}'...")

        try:
            cmd = ["networksetup", "-setairportnetwork", self.interface, ssid]
            if password:
                cmd.append(password)

            result = subprocess.run(
                cmd, capture_output=True, text=True, timeout=30
            )

            if result.returncode == 0 and not result.stderr.strip():
                # Verify connection
                time.sleep(3)
                current = self.get_current_network()
                if current and current == ssid:
                    print(f"[OK] Successfully connected to '{ssid}'")
                    self._show_connection_info()
                    return True
                else:
                    print(f"[WARN] Command succeeded but connection to '{ssid}' could not be verified.")
                    print(f"[INFO] Current network: {current or 'None'}")
                    return False
            else:
                error_msg = result.stderr.strip() or result.stdout.strip()
                if "Could not find network" in error_msg:
                    print(f"[ERROR] Network '{ssid}' not found. Make sure the Android device's Wi-Fi Direct hotspot is active.")
                elif "requires a password" in error_msg.lower() or "password" in error_msg.lower():
                    print(f"[ERROR] Incorrect password or password required for '{ssid}'.")
                else:
                    print(f"[ERROR] Failed to connect: {error_msg}")
                return False

        except subprocess.TimeoutExpired:
            print("[ERROR] Connection attempt timed out.")
            return False
        except subprocess.CalledProcessError as e:
            print(f"[ERROR] Connection failed: {e}")
            return False

    def disconnect(self) -> bool:
        """Disconnect from the current Wi-Fi network."""
        # Method 1: Toggle Wi-Fi off and on to disconnect
        try:
            subprocess.run(
                ["networksetup", "-setairportpower", self.interface, "off"],
                capture_output=True, text=True, timeout=5
            )
            time.sleep(1)
            subprocess.run(
                ["networksetup", "-setairportpower", self.interface, "on"],
                capture_output=True, text=True, timeout=5
            )
            print("[OK] Disconnected from Wi-Fi network.")
            return True
        except Exception as e:
            print(f"[ERROR] Failed to disconnect: {e}")
            return False

    def get_current_network(self) -> Optional[str]:
        """Get the SSID of the currently connected Wi-Fi network."""
        try:
            result = subprocess.run(
                ["networksetup", "-getairportnetwork", self.interface],
                capture_output=True, text=True, check=True
            )
            # Output format: "Current Wi-Fi Network: <SSID>"
            match = re.search(r"(?:Current Wi-Fi Network|Current AirPort Network):\s*(.+)", result.stdout)
            if match:
                return match.group(1).strip()
        except subprocess.CalledProcessError:
            pass
        return None

    def get_status(self) -> dict:
        """Get the current Wi-Fi connection status."""
        status = {
            "interface": self.interface,
            "power": self._is_wifi_powered_on(),
            "ssid": None,
            "ip_address": None,
            "subnet_mask": None,
            "router": None,
        }

        if status["power"]:
            status["ssid"] = self.get_current_network()

            try:
                result = subprocess.run(
                    ["ipconfig", "getifaddr", self.interface],
                    capture_output=True, text=True, timeout=5
                )
                if result.returncode == 0:
                    status["ip_address"] = result.stdout.strip()
            except Exception:
                pass

            try:
                result = subprocess.run(
                    ["networksetup", "-getinfo", "Wi-Fi"],
                    capture_output=True, text=True, timeout=5
                )
                if result.returncode == 0:
                    for line in result.stdout.split("\n"):
                        if line.startswith("Subnet mask:"):
                            status["subnet_mask"] = line.split(":")[1].strip()
                        elif line.startswith("Router:"):
                            status["router"] = line.split(":")[1].strip()
            except Exception:
                pass

        return status

    def _show_connection_info(self):
        """Display connection information after successful connection."""
        status = self.get_status()
        if status["ip_address"]:
            print(f"  IP Address : {status['ip_address']}")
        if status["subnet_mask"]:
            print(f"  Subnet Mask: {status['subnet_mask']}")
        if status["router"]:
            print(f"  Router     : {status['router']}")


def format_network_table(networks: list[dict]) -> str:
    """Format networks into a readable table."""
    if not networks:
        return "  No networks found."

    # Column widths
    ssid_w = max(len(n["ssid"]) for n in networks)
    ssid_w = max(ssid_w, 4) + 2

    header = f"  {'#':<4} {'SSID':<{ssid_w}} {'BSSID':<20} {'RSSI':<8} {'CH':<6} {'SECURITY'}"
    separator = "  " + "-" * (len(header) - 2)

    rows = [header, separator]
    for i, n in enumerate(networks, 1):
        row = f"  {i:<4} {n['ssid']:<{ssid_w}} {n.get('bssid', 'N/A'):<20} {n.get('rssi', 'N/A'):<8} {n.get('channel', 'N/A'):<6} {n.get('security', 'N/A')}"
        rows.append(row)

    return "\n".join(rows)


def cmd_scan(args, manager: WiFiDirectManager):
    """Handle the 'scan' command."""
    networks = manager.scan_networks(show_all=args.all)

    if args.all:
        print(f"\n[RESULT] Found {len(networks)} Wi-Fi network(s):\n")
    else:
        print(f"\n[RESULT] Found {len(networks)} Wi-Fi Direct network(s):\n")

    print(format_network_table(networks))

    if not networks and not args.all:
        print("\n[TIP] No Wi-Fi Direct networks found.")
        print("  - Make sure the Android device has Wi-Fi Direct hotspot enabled.")
        print("  - Android Wi-Fi Direct SSIDs typically start with 'DIRECT-'.")
        print("  - Use '--all' flag to see all available networks.")
    print()


def cmd_connect(args, manager: WiFiDirectManager):
    """Handle the 'connect' command."""
    ssid = args.ssid
    password = args.password

    if not ssid:
        # Interactive mode: scan and let user choose
        print("\n[INFO] Scanning for Wi-Fi Direct networks...\n")
        networks = manager.scan_networks(show_all=False)

        if not networks:
            print("[INFO] No Wi-Fi Direct networks found. Scanning all networks...\n")
            networks = manager.scan_networks(show_all=True)

        if not networks:
            print("[ERROR] No Wi-Fi networks found. Please check that Wi-Fi is enabled.")
            return

        print(format_network_table(networks))
        print()

        while True:
            try:
                choice = input("Enter the number of the network to connect to (or 'q' to quit): ").strip()
                if choice.lower() == 'q':
                    print("[INFO] Cancelled.")
                    return
                idx = int(choice) - 1
                if 0 <= idx < len(networks):
                    ssid = networks[idx]["ssid"]
                    break
                else:
                    print(f"[WARN] Please enter a number between 1 and {len(networks)}.")
            except ValueError:
                print("[WARN] Please enter a valid number.")
            except (KeyboardInterrupt, EOFError):
                print("\n[INFO] Cancelled.")
                return

    if not password:
        try:
            password = getpass.getpass(f"Enter password for '{ssid}' (press Enter if open): ").strip()
            if not password:
                password = None
        except (KeyboardInterrupt, EOFError):
            print("\n[INFO] Cancelled.")
            return

    manager.connect(ssid, password)


def cmd_status(args, manager: WiFiDirectManager):
    """Handle the 'status' command."""
    status = manager.get_status()

    print("\n[Wi-Fi Status]")
    print(f"  Interface  : {status['interface']}")
    print(f"  Power      : {'On' if status['power'] else 'Off'}")

    if status["power"]:
        ssid = status["ssid"] or "Not connected"
        is_direct = status["ssid"] and status["ssid"].startswith(WiFiDirectManager.WIFI_DIRECT_PREFIX)
        print(f"  Network    : {ssid}" + (" (Wi-Fi Direct)" if is_direct else ""))
        print(f"  IP Address : {status['ip_address'] or 'N/A'}")
        print(f"  Subnet Mask: {status['subnet_mask'] or 'N/A'}")
        print(f"  Router     : {status['router'] or 'N/A'}")
    print()


def cmd_disconnect(args, manager: WiFiDirectManager):
    """Handle the 'disconnect' command."""
    current = manager.get_current_network()
    if current:
        print(f"[INFO] Disconnecting from '{current}'...")
        manager.disconnect()
    else:
        print("[INFO] Not currently connected to any Wi-Fi network.")


def main():
    parser = argparse.ArgumentParser(
        description="Mac CLI tool for connecting to Android Wi-Fi Direct hotspots.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  %(prog)s scan                          Scan for Wi-Fi Direct networks
  %(prog)s scan --all                    Scan for all Wi-Fi networks
  %(prog)s connect                       Interactive connect (scan + choose)
  %(prog)s connect -s DIRECT-xx -p 1234  Connect to a specific network
  %(prog)s status                        Show current Wi-Fi status
  %(prog)s disconnect                    Disconnect from current network

Notes:
  - Android Wi-Fi Direct hotspot SSIDs typically start with 'DIRECT-'.
  - Some operations may require administrator privileges (sudo).
  - Make sure the Android device's Wi-Fi Direct hotspot is active before connecting.
        """
    )

    subparsers = parser.add_subparsers(dest="command", help="Available commands")

    # scan command
    scan_parser = subparsers.add_parser("scan", help="Scan for Wi-Fi Direct networks")
    scan_parser.add_argument("--all", "-a", action="store_true",
                             help="Show all Wi-Fi networks, not just Wi-Fi Direct")

    # connect command
    connect_parser = subparsers.add_parser("connect", help="Connect to a Wi-Fi Direct network")
    connect_parser.add_argument("-s", "--ssid", type=str, default=None,
                                help="SSID of the network to connect to")
    connect_parser.add_argument("-p", "--password", type=str, default=None,
                                help="Password for the network")

    # status command
    subparsers.add_parser("status", help="Show current Wi-Fi connection status")

    # disconnect command
    subparsers.add_parser("disconnect", help="Disconnect from current Wi-Fi network")

    args = parser.parse_args()

    if not args.command:
        parser.print_help()
        sys.exit(0)

    manager = WiFiDirectManager()

    commands = {
        "scan": cmd_scan,
        "connect": cmd_connect,
        "status": cmd_status,
        "disconnect": cmd_disconnect,
    }

    commands[args.command](args, manager)


if __name__ == "__main__":
    main()
