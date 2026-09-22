# Native Wi-Fi integration

The requested scope is macOS's own Wi-Fi selection UI, not a renamed Ethernet interface or a decorative icon. It includes scanning, switching networks, credentials, disconnect/power controls, and accurate connection status. The working0.1.22 Ethernet-profile build remains the hardware-tested fallback until the native profile is verified.

## Hardware scope and truthful capabilities

RTL8852BE certification lists 2.4/5GHz Wi-Fi and WPA2-Personal/Enterprise, WPA3-Personal/Enterprise, PMF and Enhanced Open. Source: Wi-Fi Alliance certification variant116308, https://api.cert.wi-fi.org/api/certificate/download/public?variantId=116308 . It is not a6GHz device. No unsupported6GHz/160MHz/AX capability may be advertised by the current legacy20MHz data path.

Requested access modes tracked separately:

| Mode | Current evidence / missing implementation |
|---|---|
| WPA2-Personal/CCMP | Hardware handshake, DHCP and certificate-verified external HTTPS passed on0.1.22; UI selection not yet integrated |
| Open / hidden SSID | Protocol primitives present; native selection and hardware test pending |
| WPA-Personal/TKIP and WEP legacy | Need explicit policy/crypto-path validation; not advertised as working |
| WPA2-Enterprise/802.1X | Requires Apple EAP/supplicant handoff, key installation and identity/certificate UI |
| WPA3-Personal/SAE | Requires real SAE and mandatory PMF; never claim support by silently coercing to WPA2 |
| WPA3-Enterprise | Requires actual negotiated AKM/cipher/PMF and EAP flow;192-bit suite is not implied by WPA3 branding |
| Enhanced Open/OWE | Requires OWE key agreement and validated PMF/RSN behavior; unsupported until implemented |
| WPA2/WPA3 transition | Only explicitly negotiated WPA2 is currently possible; not equivalent to pure WPA3 support |

The upstream AirportItlwm source is a reference, not evidence that RTL8852BE supports all its advertised features. Its WPA3-to-WPA2 coercion and raw key debug logging must not be copied.

## Concurrency and credential requirements

All native control requests must enter the existing hardware command gate. A network switch validates SSID, BSSID, authentication mode and PMK length before modifying the existing connection. New credentials are applied only after asynchronous disconnect, hardware actions and TX completions finish. Accepted requests are not reported as successful associations. Pending credentials are cleared on cancellation and shutdown, never published to IORegistry/logs. Binary32-byte SSIDs must not use strlen.

`WirelessSelection.hpp` implements bounded single-pending selection validation and drain barriers. `R16NetworkController::selectWirelessNetwork` and `disconnectWirelessNetwork` now enter the controller gate; the poll loop applies replacement credentials after disconnect and TX drain. These kernel-side functions are not yet connected to the native menu or hardware-tested. `native-contract.yml` compiles the pinned private declarations for offline comparison. Compilation alone does not prove private ABI compatibility or native UI functionality.

## Actual recovery inventory

Offline inspection of the user's macOS15 recovery BootKernelExtensions.kc found com.apple.iokit.IO80211Family and com.apple.iokit.IOSkywalkFamily, plus IO80211Controller, IO80211InfraProtocol and Skywalk symbols. This avoids assuming that absent standalone kext files mean the dependency is absent: the components are inside the boot collection. Private method offsets and structure sizes still require checking against the exact kernel collection before deployment.
