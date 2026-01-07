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
#endif

// Ntfy (I've never used Ntfy but it's free and you can self-host)
#ifdef USE_NTFY
  const char* NTFY_SERVER = "ntfy.sh";
  const char* NTFY_TOPIC  = "your_unique_topic_name";
  // const char* NTFY_TOKEN = "tk_your_token";  // Uncomment for private topics
  void sendNtfy(const String& message);
#endif
