# MailMon

Battery-powered ESP32 mailbox sensor with push notifications.

## Features

- **Instant notifications** when mailbox hatch opens
- **Progressive reminders** if hatch left open (5m, 30m, 2h, 6h, 12h)
- **Deep sleep** for minimal power consumption
- **Low battery warning** appended to notifications when voltage drops below 3.2V
- **Choice of notification services**: Pushover or Ntfy (self-hostable)

## Hardware

| Component | Connection |
|-----------|------------|
| ESP32 | Microcontroller |
| Reed switch | GPIO 33 → GND (closed when hatch closed) |
| LiPo battery | Via 100kΩ/100kΩ voltage divider on GPIO 34 |

### Wiring

```
LiPo+ ──┬── ESP32 VIN
        │
       100kΩ
        │
        ├── GPIO 34 (ADC)
        │
       100kΩ
        │
GND ────┴── ESP32 GND

Reed Switch: GPIO 33 ←→ GND (internal pullup used)
```

### Long Wire Runs

If the reed switch is connected via a long cable (e.g., running to an outdoor mailbox), add filtering to suppress EMI-induced false triggers:

```
GPIO 33 ──┬── 10kΩ ──── 3.3V    (external pullup, stiffer than internal)
          │
          ├── 100nF ─── GND     (noise filter capacitor)
          │
         220Ω
          │
          └── Reed Switch ── GND
```

- **10kΩ pullup**: Provides stronger pull than the internal ~45kΩ, reducing noise susceptibility
- **100nF capacitor**: Filters high-frequency EMI picked up by the cable
- **220Ω resistor**: Limits inrush current to prevent capacitor discharge welding the reed switch contacts

## Setup

1. Install [Arduino IDE](https://www.arduino.cc/en/software) with ESP32 board support
2. Clone or download this repository
3. Edit `secrets.h` with your configuration:

```cpp
#define SECRETS_H

// Choose ONE notification service
#define USE_PUSHOVER
// #define USE_NTFY

// Wi-Fi
const char* WIFI_SSID = "YourNetwork";
const char* WIFI_PASS = "YourPassword";

// Pushover (https://pushover.net - $5 one-time per platform)
#ifdef USE_PUSHOVER
  const char* PUSHOVER_TOKEN = "your_app_token";
  const char* PUSHOVER_USER  = "your_user_key";
  void sendPushover(const String& message);
#endif

// Ntfy (https://ntfy.sh - free, self-hostable)
#ifdef USE_NTFY
  const char* NTFY_SERVER = "ntfy.sh";
  const char* NTFY_TOPIC  = "your_unique_topic";
  // const char* NTFY_TOKEN = "tk_xxx";  // For private topics
  void sendNtfy(const String& message);
#endif
```

4. Open `MailMon.ino` in Arduino IDE
5. Select your ESP32 board and port
6. Upload

## Optional Optimizations

Uncomment in `MailMon.ino` to enable:

| Flag | Effect |
|------|--------|
| `USE_STATIC_IP` | Skip DHCP, saves ~1-2 seconds per wake |
| `REMEMBER_WIFI_CHANNEL` | Cache WiFi channel in RTC memory, saves ~1-2 seconds |
| `DISABLE_BROWNOUT` | Prevent brownout resets on weak batteries during WiFi init |

## How It Works

1. ESP32 sleeps until reed switch state changes (hatch open/close) or timer expires
2. On wake, reads battery voltage and door state
3. Sends notification if hatch opened, or reminder if still open
4. Configures next wake trigger and returns to deep sleep

The device draws minimal current during sleep (typically <10µA), enabling months of operation on a single LiPo charge.

## Security Note

SSL certificate validation is disabled (`setInsecure()`) to reduce memory usage and connection time. Traffic is still encrypted, but the server certificate is not verified.

## License

MIT License - see source file headers.

## Author

Ali Raheem - [github.com/ali-raheem/MailMon](https://github.com/ali-raheem/MailMon)
