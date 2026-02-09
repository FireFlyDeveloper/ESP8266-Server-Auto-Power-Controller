#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClientSecureBearSSL.h>

extern "C" {
  #include "user_interface.h"
}

// ================= CONFIG =================
#define WIFI_SSID     "ssid"
#define WIFI_PASSWORD "password"

#define API_URL       "create_api_(https://github.com/FireFlyDeveloper/UPtime.git)"
#define API_PASSWORD  "api_password"

// Pins
#define RELAY_PIN D1  // LOW = ON
#define VOLTAGE_PIN A0
#define STATUS_LED LED_BUILTIN  // LOW = ON

// Timing (ms)
#define VOLTAGE_INTERVAL 5000
#define API_INTERVAL 30000
#define WIFI_RETRY_TIME 10000
#define SAFETY_TIMEOUT 30000
#define RELAY_PULSE_MS 500
#define WATCHDOG_TIMEOUT 60000

// Voltage
#define VOLTAGE_THRESHOLD 100
#define OFFLINE_COUNT_MAX 3

// ================= RTC BOOT ID =================
struct RTCData {
  uint32_t magic;
  uint32_t bootId;
};

#define RTC_MAGIC 0xCAFEBABE
RTCData rtcData;

// ================= STATE =================
enum class DeviceState : uint8_t {
  BOOT,
  WIFI_CONNECTING,
  INIT_SYNC,
  NORMAL,
  POWER_CYCLING
};

enum class LEDMode : uint8_t {
  OFF,
  FAST,
  SLOW,
  SOLID
};

DeviceState state = DeviceState::BOOT;
LEDMode ledMode = LEDMode::OFF;

// ================= RUNTIME =================
bool wifiOK = false;
bool serverOnline = false;

uint8_t offlineCount = 0;

unsigned long tVoltage = 0;
unsigned long tApi = 0;
unsigned long tWifiRetry = 0;
unsigned long tPower = 0;
unsigned long tWatchdog = 0;

// ================= BOOT ID =================
void initBootId() {
  system_rtc_mem_read(64, &rtcData, sizeof(rtcData));

  if (rtcData.magic != RTC_MAGIC) {
    rtcData.magic = RTC_MAGIC;
    rtcData.bootId = 0;
  }

  rtcData.bootId++;
  system_rtc_mem_write(64, &rtcData, sizeof(rtcData));

  Serial.printf("[BOOT] bootId=%lu\n", rtcData.bootId);
}

// ================= SETUP =================
void setup() {
  Serial.begin(115200);
  delay(50);

  initBootId();   // MUST be first logical operation

  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, HIGH);

  pinMode(STATUS_LED, OUTPUT);
  digitalWrite(STATUS_LED, HIGH);

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  state = DeviceState::WIFI_CONNECTING;
  tWifiRetry = millis();

  Serial.println("[BOOT] Power Manager started");
}

// ================= WIFI =================
void serviceWiFi() {
  unsigned long now = millis();

  if (WiFi.status() == WL_CONNECTED) {
    if (!wifiOK) {
      wifiOK = true;
      Serial.print("[WiFi] Connected, IP=");
      Serial.println(WiFi.localIP());
      state = DeviceState::INIT_SYNC;
    }
    return;
  }

  wifiOK = false;

  if (now - tWifiRetry >= WIFI_RETRY_TIME) {
    tWifiRetry = now;
    Serial.println("[WiFi] Retry");
    WiFi.disconnect();
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  }
}

// ================= LED =================
void serviceLED() {
  static unsigned long tBlink = 0;
  static bool led = false;
  unsigned long now = millis();

  switch (ledMode) {
    case LEDMode::OFF:
      digitalWrite(STATUS_LED, HIGH);
      break;

    case LEDMode::FAST:
      if (now - tBlink >= 200) {
        tBlink = now;
        led = !led;
        digitalWrite(STATUS_LED, led ? LOW : HIGH);
      }
      break;

    case LEDMode::SLOW:
      if (now - tBlink >= 1000) {
        tBlink = now;
        led = !led;
        digitalWrite(STATUS_LED, led ? LOW : HIGH);
      }
      break;

    case LEDMode::SOLID:
      digitalWrite(STATUS_LED, LOW);
      break;
  }
}

// ================= VOLTAGE =================
bool readVoltage() {
  int v = analogRead(VOLTAGE_PIN);
  bool online = (v > VOLTAGE_THRESHOLD);
  Serial.printf("[V] %d -> %s\n", v, online ? "ON" : "OFF");
  return online;
}

// ================= API =================
void sendApi() {
  if (!wifiOK) return;

  BearSSL::WiFiClientSecure client;
  client.setInsecure();
  client.setTimeout(5000);

  HTTPClient https;

  String body;
  body.reserve(128);
  body = "{";
  body += "\"isOnline\":";
  body += serverOnline ? "true" : "false";
  body += ",\"bootId\":";
  body += rtcData.bootId;
  body += ",\"password\":\"";
  body += API_PASSWORD;
  body += "\"}";

  if (https.begin(client, API_URL)) {
    https.addHeader("Content-Type", "application/json");
    int code = https.POST(body);
    Serial.printf("[API] POST -> %d\n", code);
    https.end();
  }
}

// ================= POWER =================
void startPowerCycle() {
  if (millis() - tPower < SAFETY_TIMEOUT) return;

  Serial.println("[Relay] Power cycle");
  state = DeviceState::POWER_CYCLING;
  ledMode = LEDMode::SOLID;

  digitalWrite(RELAY_PIN, LOW);
  delay(RELAY_PULSE_MS);
  digitalWrite(RELAY_PIN, HIGH);

  tPower = millis();
  offlineCount = 0;
  serverOnline = false;

  sendApi();

  state = DeviceState::NORMAL;
  ledMode = LEDMode::OFF;
}

// ================= STATE MACHINE =================
void runStateMachine() {
  unsigned long now = millis();

  switch (state) {

    case DeviceState::WIFI_CONNECTING:
      ledMode = LEDMode::FAST;
      break;

    case DeviceState::INIT_SYNC:
      ledMode = LEDMode::SLOW;
      serverOnline = readVoltage();
      sendApi();  // INITIAL BOOT SYNC (WITH bootId)
      tApi = now;
      state = DeviceState::NORMAL;
      break;

    case DeviceState::NORMAL:
      ledMode = LEDMode::OFF;

      if (now - tVoltage >= VOLTAGE_INTERVAL) {
        tVoltage = now;
        bool onlineNow = readVoltage();

        if (onlineNow != serverOnline) {
          serverOnline = onlineNow;
          offlineCount = 0;
          sendApi();
        } else if (!onlineNow) {
          if (++offlineCount >= OFFLINE_COUNT_MAX) {
            startPowerCycle();
          }
        } else {
          offlineCount = 0;
        }
      }

      if (now - tApi >= API_INTERVAL) {
        tApi = now;
        sendApi();
      }
      break;

    case DeviceState::POWER_CYCLING:
      break;

    default:
      break;
  }
}

// ================= WATCHDOG =================
void serviceWatchdog() {
  if (millis() - tWatchdog > WATCHDOG_TIMEOUT) {
    Serial.println("[WDT] Restart");
    ESP.restart();
  }
  tWatchdog = millis();
}

// ================= LOOP =================
void loop() {
  serviceWiFi();

  if (wifiOK) {
    runStateMachine();
  }

  serviceLED();
  serviceWatchdog();
  delay(2);
}
