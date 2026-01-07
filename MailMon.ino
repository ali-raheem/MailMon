/*
 * MailMon
 * 
 * ESP32-based mailbox sensor with push notifications
 * Insecure WiFi (does not check SSL certifcate)
 * 
 * Author: Ali Raheem
 * License: MIT
 * Repository: https://github.com/ali-raheem/MailMon
 * 
 * Hardware:
 *   - ESP32
 *   - Reed switch on GPIO 33 - Closed when hatch closed connected to ground
 *   - LiPo battery with voltage divider on GPIO 34 (100k/100k to ground)
 */

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"
#include "esp_task_wdt.h"
#include "esp_system.h"

// ==================== Configuration ====================
 
#include "secrets.h"

// Fallback config if secrets.h not customized

#ifndef SECRETS_H
#define SECRETS_H
// Choose ONE notification service
#define USE_PUSHOVER
// #define USE_NTFY

// Wi-Fi
const char* WIFI_SSID = "YOUR_WIFI_NAME";
const char* WIFI_PASS = "YOUR_WIFI_PASSWORD";

// Pushover (requies one time $5 purchase per device platform)
#ifdef USE_PUSHOVER
  const char* PUSHOVER_TOKEN = "YOUR_APP_TOKEN";
  const char* PUSHOVER_USER  = "YOUR_USER_KEY";
  void sendPushover(const String& message);
#endif // USE_PUSHOVER

// Ntfy (I've never used Ntfy but it's free and you can self-host)
#ifdef USE_NTFY
  const char* NTFY_SERVER = "ntfy.sh";
  const char* NTFY_TOPIC  = "your_unique_topic_name";
  // const char* NTFY_TOKEN = "tk_your_token";  // Uncomment for private topics
  void sendNtfy(const String& message);
#endif // USE_NTFY

#endif // SECRETS_H

// Hardware config
constexpr int REED_PIN = 33;              // Reed switch (connects to GND when closed)
constexpr int BATTERY_PIN = 34;           // Voltage divider (100kΩ + 100kΩ)
constexpr float LOW_BATT_LIMIT = 3.2f;    // LiPo critical voltage
constexpr float VOLTAGE_MULTIPLIER = 0.00161f;

// Will REED_PIN be high or low if door open/closed?
constexpr int DOOR_OPEN = HIGH;
constexpr int DOOR_CLOSED = !DOOR_OPEN;

// Nag schedule in minutes: 5m, 30m, 2h, 6h, 12h
const int NAG_SCHEDULE[] = {5, 30, 120, 360, 720};
constexpr int NAG_SCHEDULE_SIZE = sizeof(NAG_SCHEDULE) / sizeof(NAG_SCHEDULE[0]);

// Watchdog timer
constexpr int WDT_TIMEOUT = 20;

// Uncomment to disable brownout detector (if crash on wifi init)
// #define DISABLE_BROWNOUT

// Uncomment for static IP (can save about 1-2 seconds of runtime)
// #define USE_STATIC_IP

#ifdef USE_STATIC_IP
  IPAddress local_IP(192, 168, 1, 200);
  IPAddress gateway(192, 168, 1, 1);
  IPAddress subnet(255, 255, 255, 0);
  IPAddress primaryDNS(8, 8, 8, 8);
#endif

// Uncomment to remember WiFi channel (can save about 1-2 seconds runtime)
#define REMEMBER_WIFI_CHANNEL

// Check notification settings
#if defined(USE_PUSHOVER) && defined(USE_NTFY)
  #error "Cannot use both Pushover and Ntfy! Choose one."
#endif
#if !defined(USE_PUSHOVER) && !defined(USE_NTFY)
  #error "Must define either USE_PUSHOVER or USE_NTFY"
#endif

// Rate limit open hatch nags
RTC_DATA_ATTR int nag_count = 0;

#ifdef REMEMBER_WIFI_CHANNEL
  RTC_DATA_ATTR int saved_channel = 0;
  RTC_DATA_ATTR uint32_t rtc_magic = 0;
  constexpr uint32_t RTC_MAGIC_VALUE = 0xCAFEBABE;
#endif


void setup() {
  Serial.begin(115200);
  
  // Check WDT reset
  esp_reset_reason_t reset_reason = esp_reset_reason();
  if (reset_reason == ESP_RST_TASK_WDT || reset_reason == ESP_RST_WDT) {
    Serial.println("WDT recovery - skipping notification this cycle");
    delay(1000);
    esp_deep_sleep_start();
  }
  
  // Initialize watchdog
  esp_task_wdt_config_t wdt_config = {
    .timeout_ms = WDT_TIMEOUT * 1000,
    .idle_core_mask = (1 << 0),
    .trigger_panic = true
  };
  esp_task_wdt_init(&wdt_config);
  esp_task_wdt_add(NULL);
  
  pinMode(REED_PIN, INPUT_PULLUP);
  analogReadResolution(12);
  analogSetAttenuation(ADC_11db);
  
  esp_task_wdt_reset();
  
  // Read sensors
  float voltage = readBatteryVoltage();
  String battInfo = (voltage < LOW_BATT_LIMIT) ? " (Low Batt: " + String(voltage, 2) + "V)" : "";
  bool doorOpen = isDoorOpen();

  esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();

  esp_task_wdt_reset();
  
  // Handle wakeup events
  if (wakeup_reason == ESP_SLEEP_WAKEUP_EXT0) {
    // Pin state changed
    if (doorOpen) {
      nag_count = 0;
      sendNotification("You've got mail! Hatch opened." + battInfo);
    } else {
      nag_count = 0;
      Serial.println("Hatch closed.");
    }
  } 
  else if (wakeup_reason == ESP_SLEEP_WAKEUP_TIMER) {
    // Door hasn't been closed
    if (doorOpen) {
      nag_count++;
      sendNotification("Reminder #" + String(nag_count) + ": Hatch STILL open." + battInfo);
    }
  }
  else {
    // First boot or manual reset
    Serial.println("System boot");
    nag_count = 0;
    #ifdef REMEMBER_WIFI_CHANNEL
      rtc_magic = 0;  // Invalidate saved channel
    #endif
    if (doorOpen) {
      Serial.println("Hatch is open - starting nag timer (no initial notification)");
    } else {
      Serial.println("Hatch is closed");
    }
  }

  esp_task_wdt_reset();

  // Configure sleep mode based on door state
  if (doorOpen) {
    // Door open - set timer for next nag, wake on close
    int nag_index = min(nag_count, NAG_SCHEDULE_SIZE - 1);
    int next_nag_mins = NAG_SCHEDULE[nag_index];
    
    Serial.print("Door OPEN: Next nag in ");
    Serial.print(next_nag_mins);
    Serial.println(" mins");
    
    uint64_t time_us = (uint64_t)next_nag_mins * 60 * 1000000ULL;
    esp_sleep_enable_timer_wakeup(time_us);
    esp_sleep_enable_ext0_wakeup((gpio_num_t)REED_PIN, DOOR_CLOSED);  // Wake on LOW
  } 
  else {
    // Door closed - deep sleep until opened
    Serial.println("Door CLOSED: Deep sleep until open");
    esp_sleep_enable_timer_wakeup(24ULL * 60 * 60 * 1000000ULL); // 24 hour timeout to disable
    esp_sleep_enable_ext0_wakeup((gpio_num_t)REED_PIN, DOOR_OPEN);  // Wake on HIGH
  }
  
  // Clean shutdown
  esp_task_wdt_delete(NULL);
  Serial.flush();
  esp_deep_sleep_start();
}

void loop() {}

// ==================== FUNCTIONS ====================

float readBatteryVoltage() {
  // Average 10 samples to reduce noise
  float total = 0;
  for (int i = 0; i < 10; i++) {
    total += analogRead(BATTERY_PIN);
    delay(2);
  }
  return (total / 10.0) * VOLTAGE_MULTIPLIER;
}

void sendNotification(const String& message) {
  Serial.print("Sending via ");
  #ifdef USE_PUSHOVER
    Serial.print("Pushover");
  #else
    Serial.print("Ntfy");
  #endif
  Serial.println(": " + message);
  
  #ifdef DISABLE_BROWNOUT
    WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);
  #endif
  
  WiFi.mode(WIFI_STA);
  
  #ifdef USE_STATIC_IP
    if (!WiFi.config(local_IP, gateway, subnet, primaryDNS)) {
      Serial.println("Static IP config failed");
    }
  #endif
  
  // Use saved channel if valid
  #ifdef REMEMBER_WIFI_CHANNEL
    if (rtc_magic == RTC_MAGIC_VALUE && saved_channel >= 1 && saved_channel <= 13) {
      Serial.print("Using saved channel: ");
      Serial.println(saved_channel);
      WiFi.begin(WIFI_SSID, WIFI_PASS, saved_channel);
    } else {
      Serial.println("Scanning for WiFi...");
      WiFi.begin(WIFI_SSID, WIFI_PASS);
    }
  #else
    WiFi.begin(WIFI_SSID, WIFI_PASS);
  #endif
  
  // Wait max 10 seconds for connection
  int timeout = 0;
  while (WiFi.status() != WL_CONNECTED && timeout < 20) {
    delay(500);
    Serial.print(".");
    timeout++;
    
    if (timeout % 4 == 0) {  // Feed WDT every 2 seconds
      esp_task_wdt_reset();
    }
  }
  
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("\nWiFi failed!");
    #ifdef REMEMBER_WIFI_CHANNEL
      rtc_magic = 0;  // Invalidate on failure
    #endif
    return;
  }
  
  Serial.println("\nConnected!");
  
  #ifdef REMEMBER_WIFI_CHANNEL
    saved_channel = WiFi.channel();
    rtc_magic = RTC_MAGIC_VALUE;
    Serial.print("Saved channel: ");
    Serial.println(saved_channel);
  #endif
  
  esp_task_wdt_reset();
  
  // Send notification
  #ifdef USE_PUSHOVER
    sendPushover(message);
  #else
    sendNtfy(message);
  #endif
  
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
}

#ifdef USE_PUSHOVER
String urlEncode(const String& str) {
  String encoded = "";
  encoded.reserve(str.length() * 1.5);

  for (unsigned int i = 0; i < str.length(); i++) {
    char c = str.charAt(i);
    if (c == ' ') {
      encoded += '+';
    } else if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
      encoded += c;
    } else {
      encoded += '%';
      char hex[3];
      sprintf(hex, "%02X", (unsigned char)c);
      encoded += hex;
    }
  }
  return encoded;
}

void sendPushover(const String& message) {
  WiFiClientSecure client;
  client.setInsecure();
  
  if (!client.connect("api.pushover.net", 443)) {
    Serial.println("Pushover connection failed");
    return;
  }
  
  String postData = "token=" + String(PUSHOVER_TOKEN) + 
                    "&user=" + String(PUSHOVER_USER) + 
                    "&message=" + urlEncode(message);
  
  client.println("POST /1/messages.json HTTP/1.1");
  client.println("Host: api.pushover.net");
  client.println("Connection: close");
  client.println("Content-Type: application/x-www-form-urlencoded");
  client.print("Content-Length: ");
  client.println(postData.length());
  client.println();
  client.print(postData);
  
  // Wait for response
  unsigned long start = millis();
  while (client.connected() && millis() - start < 2000) {
    if (client.available()) {
      String line = client.readStringUntil('\n');
      if (line.indexOf("200 OK") >= 0 || line.indexOf("\"status\":1") >= 0) {
        Serial.println("Sent successfully!");
        break;
      }
    }
  }
  
  client.stop();
}
#endif

#ifdef USE_NTFY
void sendNtfy(const String& message) {
  WiFiClientSecure client;
  client.setInsecure();
  
  if (!client.connect(NTFY_SERVER, 443)) {
    Serial.println("Ntfy connection failed");
    return;
  }
  
  String path = "/" + String(NTFY_TOPIC);
  
  client.print("POST ");
  client.print(path);
  client.println(" HTTP/1.1");
  client.print("Host: ");
  client.println(NTFY_SERVER);
  client.println("Content-Type: text/plain");
  
  #ifdef NTFY_TOKEN
    client.print("Authorization: Bearer ");
    client.println(NTFY_TOKEN);
  #endif
  
  client.print("Content-Length: ");
  client.println(message.length());
  client.println("Connection: close");
  client.println();
  client.print(message);
  
  // Wait for response
  unsigned long start = millis();
  while (client.connected() && millis() - start < 2000) {
    if (client.available()) {
      String line = client.readStringUntil('\n');
      if (line.indexOf("200 OK") >= 0) {
        Serial.println("Sent successfully!");
        break;
      }
    }
  }
  
  client.stop();
}
#endif

bool isDoorOpen() {
  return digitalRead(REED_PIN) == DOOR_OPEN;
}