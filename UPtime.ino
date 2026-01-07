#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClientSecureBearSSL.h>

// ================= CONFIG =================
#define WIFI_SSID     "ssid"
#define WIFI_PASSWORD "password"

#define API_URL       "create_api_(https://github.com/FireFlyDeveloper/UPtime.git)"
#define API_PASSWORD  "api_password"

// Pins
#define RELAY_PIN     D1      // LOW = ON
#define VOLTAGE_PIN  A0
#define STATUS_LED   LED_BUILTIN  // LOW = ON

// Timing (ms)
#define VOLTAGE_INTERVAL   5000
#define API_INTERVAL       30000
#define WIFI_RETRY_TIME    10000
#define SAFETY_TIMEOUT     30000
#define RELAY_PULSE_MS     500
#define WATCHDOG_TIMEOUT   60000

// Voltage
#define VOLTAGE_THRESHOLD  100
#define OFFLINE_COUNT_MAX  3

// ================= STATE =================
enum DeviceState : uint8_t {
  BOOT,
  WIFI_CONNECTING,
  NORMAL,
  POWER_CYCLING
};

enum LEDMode : uint8_t {
  LED_IDLE,
  LED_FAST,
  LED_SLOW,
  LED_SOLID
};

DeviceState state = BOOT;
LEDMode ledMode = LED_IDLE;

bool wifiOK = false;
bool serverOnline = true;

uint8_t offlineCount = 0;

unsigned long tVoltage   = 0;
unsigned long tApi       = 0;
unsigned long tWifiRetry = 0;
unsigned long tPower     = 0;
unsigned long tWatchdog  = 0;

// ================= SETUP =================
void setup() {
  Serial.begin(115200);
  delay(50);

  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, HIGH);

  pinMode(STATUS_LED, OUTPUT);
  digitalWrite(STATUS_LED, HIGH);

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  state = WIFI_CONNECTING;
  tWifiRetry = millis();

  Serial.println("\n[BOOT] Power Manager started");
}

// ================= WIFI =================
void serviceWiFi() {
  unsigned long now = millis();

  if (WiFi.status() == WL_CONNECTED) {
    if (!wifiOK) {
      wifiOK = true;
      Serial.print("[WiFi] Connected, IP=");
      Serial.println(WiFi.localIP());
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
    case LED_IDLE:
      digitalWrite(STATUS_LED, HIGH);
      break;

    case LED_FAST:
      if (now - tBlink >= 200) {
        tBlink = now;
        led = !led;
        digitalWrite(STATUS_LED, led ? LOW : HIGH);
      }
      break;

    case LED_SLOW:
      if (now - tBlink >= 1000) {
        tBlink = now;
        led = !led;
        digitalWrite(STATUS_LED, led ? LOW : HIGH);
      }
      break;

    case LED_SOLID:
      digitalWrite(STATUS_LED, LOW);
      break;
  }
}

// ================= VOLTAGE =================
void serviceVoltage() {
  unsigned long now = millis();
  if (now - tVoltage < VOLTAGE_INTERVAL) return;
  tVoltage = now;

  int v = analogRead(VOLTAGE_PIN);
  bool onlineNow = (v > VOLTAGE_THRESHOLD);

  Serial.printf("[V] %d -> %s\n", v, onlineNow ? "ON" : "OFF");

  if (onlineNow != serverOnline) {
    serverOnline = onlineNow;
    offlineCount = 0;
    sendApi();
  } else if (!serverOnline) {
    if (++offlineCount >= OFFLINE_COUNT_MAX) {
      triggerPowerCycle();
    }
  } else {
    offlineCount = 0;
  }
}

// ================= RELAY =================
void triggerPowerCycle() {
  unsigned long now = millis();
  if (now - tPower < SAFETY_TIMEOUT) return;

  Serial.println("[Relay] Power cycle");
  state = POWER_CYCLING;
  ledMode = LED_SOLID;

  digitalWrite(RELAY_PIN, LOW);
  delay(RELAY_PULSE_MS);
  digitalWrite(RELAY_PIN, HIGH);

  tPower = now;
  offlineCount = 0;
  serverOnline = false;

  sendApi();

  state = NORMAL;
  ledMode = LED_IDLE;
}

// ================= API =================
void sendApi() {
  if (!wifiOK) return;

  static BearSSL::WiFiClientSecure client;
  static HTTPClient https;

  client.setInsecure();
  client.setTimeout(5000);

  String body;
  body.reserve(96);
  body = "{\"isOnline\":";
  body += serverOnline ? "true" : "false";
  body += ",\"password\":\"";
  body += API_PASSWORD;
  body += "\"}";

  if (https.begin(client, API_URL)) {
    https.addHeader("Content-Type", "application/json");
    int code = https.POST(body);
    Serial.printf("[API] %d\n", code);
    https.end();
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

  if (!wifiOK) {
    state = WIFI_CONNECTING;
    ledMode = LED_FAST;
  } else if (state != POWER_CYCLING) {
    state = NORMAL;
    ledMode = LED_IDLE;
  }

  serviceVoltage();

  if (wifiOK && millis() - tApi >= API_INTERVAL) {
    tApi = millis();
    sendApi();
  }

  serviceLED();
  serviceWatchdog();

  delay(2);  // yield to WiFi stack
}
