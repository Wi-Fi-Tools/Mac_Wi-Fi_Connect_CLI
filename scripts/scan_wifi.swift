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
        print("\(ssid)|\(bssid)|\(rssi)|\(channel)|\(security)")
    }
} catch {
    fputs("ERROR: \(error.localizedDescription)\n", stderr)
    exit(1)
}
