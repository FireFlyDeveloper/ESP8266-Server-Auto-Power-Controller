#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClientSecureBearSSL.h>
#include <Ticker.h>

// ========== CONFIGURABLE PARAMETERS ==========
const char* WIFI_SSID = "ssid";
const char* WIFI_PASSWORD = "password";

const char* API_URL = "your_api_here";
const char* API_PASSWORD = "password";

// Pin assignments
const int RELAY_PIN = D1;        // LOW = ON, HIGH = OFF
const int VOLTAGE_PIN = A0;
const int STATUS_LED = LED_BUILTIN;

// Timing parameters (all in milliseconds)
const unsigned long VOLTAGE_CHECK_INTERVAL = 5000;      // 5 seconds
const unsigned long API_REPORT_INTERVAL = 30000;        // 30 seconds
const unsigned long RELAY_PULSE_DURATION = 500;         // 500ms
const unsigned long SAFETY_TIMEOUT = 30000;             // 30 seconds between cycles
const unsigned long FAILED_API_RETRY_INTERVAL = 30000;  // 30 seconds
const unsigned long WIFI_CONNECT_TIMEOUT = 30000;       // 30 seconds max to connect

// Voltage threshold (0-1023, default 100 ≈ 0.5V)
const int VOLTAGE_THRESHOLD = 100;

// Detection parameters
const int OFFLINE_CONSECUTIVE_COUNT = 3;  // 3 readings before triggering
const int MAX_QUEUE_SIZE = 3;             // Max queued API calls

// ========== GLOBAL VARIABLES ==========
Ticker voltageCheckTicker;
Ticker apiReportTicker;
Ticker ledBlinkTicker;

enum DeviceState {
  STATE_BOOT,
  STATE_WIFI_CONNECTING,
  STATE_NORMAL,
  STATE_OFFLINE_DETECTED,
  STATE_POWER_CYCLING,
  STATE_SAFETY_TIMEOUT
};

enum LEDPattern {
  LED_OFF,
  LED_ON,
  LED_BLINK_FAST,    // WiFi connecting
  LED_BLINK_SLOW,    // General error/attention
  LED_PULSE_ONCE     // For power cycling
};

struct QueuedAPIRequest {
  bool isOnline;
  unsigned long timestamp;
};

// State management
volatile DeviceState currentState = STATE_BOOT;
volatile LEDPattern currentLEDPattern = LED_OFF;
volatile bool ledState = false;
volatile unsigned long lastLEDUpdate = 0;

// Voltage monitoring
volatile int offlineCount = 0;
volatile bool serverOnline = true;
volatile unsigned long lastVoltageCheck = 0;
volatile unsigned long lastPowerCycleTime = 0;

// API management
volatile unsigned long lastApiReport = 0;
QueuedAPIRequest apiQueue[MAX_QUEUE_SIZE];
volatile int queueHead = 0;
volatile int queueTail = 0;
volatile int queueSize = 0;

// WiFi management
volatile bool wifiConnected = false;
volatile unsigned long lastWifiCheck = 0;
const unsigned long WIFI_CHECK_INTERVAL = 10000;  // 10 seconds

// Watchdog
volatile unsigned long lastWatchdogReset = 0;
const unsigned long WATCHDOG_TIMEOUT = 60000;  // 1 minute

// Sleep simulation (instead of actual deep sleep)
volatile unsigned long nextVoltageCheckTime = 0;
volatile unsigned long nextApiReportTime = 0;
volatile bool isSleeping = false;

// ========== FUNCTION PROTOTYPES ==========
void setup();
void loop();
void setupPins();
void connectToWiFi();
void checkWiFi();
void checkVoltage();
void processVoltageReading(int reading);
void triggerPowerCycle();
void performApiReport(bool isOnline);
void queueApiRequest(bool isOnline);
void processApiQueue();
void updateLED();
void setLEDPattern(LEDPattern pattern);
void handleLEDBlinkFast();
void handleLEDPulseOnce();
void resetWatchdog();
void checkWatchdog();
void manageLowPowerMode();
void printStatus();

// ========== SETUP ==========
void setup() {
  Serial.begin(115200);
  delay(100);  // Give serial time to initialize
  Serial.println("\n\n=== Server Power Manager Starting ===");
  Serial.println("Serial debugging: VERBOSE");
  
  setupPins();
  setLEDPattern(LED_BLINK_FAST);
  currentState = STATE_WIFI_CONNECTING;
  
  connectToWiFi();
  
  // Initialize timing
  unsigned long now = millis();
  nextVoltageCheckTime = now + VOLTAGE_CHECK_INTERVAL;
  nextApiReportTime = now + API_REPORT_INTERVAL;
  
  Serial.println("Setup complete. Starting main loop...");
  printStatus();
  
  resetWatchdog();
}

void setupPins() {
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, HIGH);  // Relay OFF initially
  
  pinMode(STATUS_LED, OUTPUT);
  digitalWrite(STATUS_LED, HIGH);
  
  pinMode(VOLTAGE_PIN, INPUT);
  
  Serial.println("Pins initialized:");
  Serial.printf("  RELAY_PIN: D1 (GPIO5) - %s\n", digitalRead(RELAY_PIN) ? "HIGH(OFF)" : "LOW(ON)");
  Serial.printf("  STATUS_LED: GPIO2\n");
  Serial.printf("  VOLTAGE_PIN: A0\n");
}

// ========== WIFI FUNCTIONS ==========
void connectToWiFi() {
  Serial.print("Connecting to WiFi: ");
  Serial.println(WIFI_SSID);
  
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  
  unsigned long startTime = millis();
  
  while (WiFi.status() != WL_CONNECTED && (millis() - startTime) < WIFI_CONNECT_TIMEOUT) {
    delay(100);
    updateLED();  // Keep LED blinking during connection
    Serial.print(".");
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    wifiConnected = true;
    setLEDPattern(LED_OFF);  // Turn off LED when connected
    Serial.println("\nWiFi connected!");
    Serial.printf("IP Address: %s\n", WiFi.localIP().toString().c_str());
    Serial.printf("RSSI: %d dBm\n", WiFi.RSSI());
  } else {
    Serial.println("\nWiFi connection failed!");
    Serial.println("Continuing without WiFi - voltage monitoring active");
    setLEDPattern(LED_BLINK_SLOW);
  }
}

void checkWiFi() {
  static unsigned long lastCheck = 0;
  unsigned long now = millis();
  
  if (now - lastCheck >= WIFI_CHECK_INTERVAL) {
    lastCheck = now;
    
    bool previousState = wifiConnected;
    wifiConnected = (WiFi.status() == WL_CONNECTED);
    
    if (!previousState && wifiConnected) {
      Serial.println("WiFi reconnected!");
      setLEDPattern(LED_OFF);
      // Process any queued API requests
      processApiQueue();
    } else if (previousState && !wifiConnected) {
      Serial.println("WiFi disconnected!");
      setLEDPattern(LED_BLINK_SLOW);
    }
  }
}

// ========== VOLTAGE MONITORING ==========
void checkVoltage() {
  unsigned long now = millis();
  
  if (now >= nextVoltageCheckTime) {
    nextVoltageCheckTime = now + VOLTAGE_CHECK_INTERVAL;
    
    int reading = analogRead(VOLTAGE_PIN);
    Serial.printf("[Voltage] Reading: %d, Threshold: %d", reading, VOLTAGE_THRESHOLD);
    
    processVoltageReading(reading);
    resetWatchdog();
  }
}

void processVoltageReading(int reading) {
  bool isOnlineNow = (reading > VOLTAGE_THRESHOLD);
  
  if (isOnlineNow != serverOnline) {
    Serial.printf(" - State changed: %s -> %s\n", 
                  serverOnline ? "Online" : "Offline",
                  isOnlineNow ? "Online" : "Offline");
    serverOnline = isOnlineNow;
    offlineCount = 0;
    
    // Report state change immediately if WiFi available
    if (wifiConnected) {
      performApiReport(serverOnline);
    } else {
      queueApiRequest(serverOnline);
    }
  } else {
    Serial.println();  // Just newline
    
    if (!serverOnline) {
      offlineCount++;
      Serial.printf("[Voltage] Offline count: %d/%d\n", offlineCount, OFFLINE_CONSECUTIVE_COUNT);
      
      if (offlineCount >= OFFLINE_CONSECUTIVE_COUNT) {
        Serial.println("[Voltage] Offline threshold reached!");
        triggerPowerCycle();
      }
    } else {
      offlineCount = 0;  // Reset if online
    }
  }
}

// ========== RELAY CONTROL ==========
void triggerPowerCycle() {
  unsigned long now = millis();
  
  // Check safety timeout
  if (lastPowerCycleTime > 0 && (now - lastPowerCycleTime) < SAFETY_TIMEOUT) {
    Serial.printf("[Safety] Skipping power cycle. Time since last: %lu ms (min: %lu ms)\n", 
                  now - lastPowerCycleTime, SAFETY_TIMEOUT);
    return;
  }
  
  Serial.println("[Relay] Triggering power cycle...");
  currentState = STATE_POWER_CYCLING;
  setLEDPattern(LED_PULSE_ONCE);
  
  // Activate relay
  digitalWrite(RELAY_PIN, LOW);  // LOW = ON
  Serial.printf("[Relay] ON (LOW) for %lu ms\n", RELAY_PULSE_DURATION);
  delay(RELAY_PULSE_DURATION);  // Blocking during pulse for reliability
  
  // Deactivate relay
  digitalWrite(RELAY_PIN, HIGH);  // HIGH = OFF
  Serial.println("[Relay] OFF (HIGH)");
  
  lastPowerCycleTime = now;
  offlineCount = 0;  // Reset counter after power cycle
  
  // Report power cycle event
  if (wifiConnected) {
    performApiReport(false);  // Server is offline after power cycle
  } else {
    queueApiRequest(false);
  }
  
  // Update next check times
  nextVoltageCheckTime = millis() + VOLTAGE_CHECK_INTERVAL;
  if (nextApiReportTime < millis()) {
    nextApiReportTime = millis() + API_REPORT_INTERVAL;
  }
  
  currentState = STATE_NORMAL;
  setLEDPattern(LED_OFF);
  Serial.println("[Relay] Power cycle complete");
}

// ========== API FUNCTIONS ==========
void performApiReport(bool isOnline) {
  if (!wifiConnected) {
    Serial.println("[API] WiFi not connected, queuing request");
    queueApiRequest(isOnline);
    return;
  }
  
  Serial.printf("[API] Sending report: isOnline=%s\n", isOnline ? "true" : "false");
  
  // Prepare JSON body
  String body = "{\"isOnline\":";
  body += isOnline ? "true" : "false";
  body += ", \"password\": \"";
  body += API_PASSWORD;
  body += "\"}";
  
  // Create secure client
  std::unique_ptr<BearSSL::WiFiClientSecure> client(new BearSSL::WiFiClientSecure);
  client->setInsecure();
  client->setTimeout(2000);  // 2 second timeout
  
  HTTPClient https;
  bool success = false;
  
  if (https.begin(*client, API_URL)) {
    https.addHeader("Content-Type", "application/json");
    
    unsigned long startTime = millis();
    int code = https.POST(body);
    unsigned long duration = millis() - startTime;
    
    if (code == 200) {
      Serial.printf("[API] SUCCESS - HTTP %d (%lums)\n", code, duration);
      success = true;
    } else {
      Serial.printf("[API] FAILED - HTTP %d (%lums)\n", code, duration);
      // Queue for retry
      queueApiRequest(isOnline);
    }
    https.end();
  } else {
    Serial.println("[API] FAILED - Could not begin connection");
    queueApiRequest(isOnline);
  }
  
  resetWatchdog();
}

void queueApiRequest(bool isOnline) {
  if (queueSize >= MAX_QUEUE_SIZE) {
    // Discard oldest (queueHead)
    queueHead = (queueHead + 1) % MAX_QUEUE_SIZE;
    queueSize--;
    // No logging as requested
  }
  
  // Add to queue
  apiQueue[queueTail].isOnline = isOnline;
  apiQueue[queueTail].timestamp = millis();
  queueTail = (queueTail + 1) % MAX_QUEUE_SIZE;
  queueSize++;
  
  Serial.printf("[Queue] Added request: isOnline=%s, Queue size: %d/%d\n", 
                isOnline ? "true" : "false", queueSize, MAX_QUEUE_SIZE);
}

void processApiQueue() {
  if (!wifiConnected || queueSize == 0) {
    return;
  }
  
  Serial.printf("[Queue] Processing %d queued requests\n", queueSize);
  
  while (queueSize > 0) {
    QueuedAPIRequest request = apiQueue[queueHead];
    unsigned long age = millis() - request.timestamp;
    
    Serial.printf("[Queue] Sending queued request (age: %lu ms): isOnline=%s\n", 
                  age, request.isOnline ? "true" : "false");
    
    performApiReport(request.isOnline);
    
    // Remove from queue
    queueHead = (queueHead + 1) % MAX_QUEUE_SIZE;
    queueSize--;
    
    // Small delay between requests
    delay(100);
  }
  
  Serial.println("[Queue] All queued requests processed");
}

// ========== LED CONTROL ==========
void updateLED() {
  unsigned long now = millis();
  
  switch (currentLEDPattern) {
    case LED_OFF:
      digitalWrite(STATUS_LED, HIGH);
      break;
      
    case LED_ON:
      digitalWrite(STATUS_LED, LOW);
      break;
      
    case LED_BLINK_FAST:
      handleLEDBlinkFast();
      break;
      
    case LED_BLINK_SLOW:
      if (now - lastLEDUpdate >= 1000) {  // 1 second interval
        lastLEDUpdate = now;
        ledState = !ledState;
        digitalWrite(STATUS_LED, ledState ? LOW : HIGH);
      }
      break;
      
    case LED_PULSE_ONCE:
      handleLEDPulseOnce();
      break;
  }
}

void handleLEDBlinkFast() {
  unsigned long now = millis();
  if (now - lastLEDUpdate >= 200) {  // 200ms interval for fast blink
    lastLEDUpdate = now;
    ledState = !ledState;
    digitalWrite(STATUS_LED, ledState ? LOW : HIGH);
  }
}

void handleLEDPulseOnce() {
  static bool pulseActive = false;
  static unsigned long pulseStart = 0;
  unsigned long now = millis();
  
  if (!pulseActive) {
    pulseActive = true;
    pulseStart = now;
    digitalWrite(STATUS_LED, LOW);
  } else if (now - pulseStart >= RELAY_PULSE_DURATION) {
    pulseActive = false;
    digitalWrite(STATUS_LED, HIGH);
    setLEDPattern(LED_OFF);  // Return to normal
  }
}

void setLEDPattern(LEDPattern pattern) {
  if (currentLEDPattern != pattern) {
    currentLEDPattern = pattern;
    lastLEDUpdate = millis();
    ledState = false;
    
    const char* patternNames[] = {"OFF", "ON", "BLINK_FAST", "BLINK_SLOW", "PULSE_ONCE"};
    Serial.printf("[LED] Pattern changed to: %s\n", patternNames[pattern]);
  }
}

// ========== WATCHDOG ==========
void resetWatchdog() {
  lastWatchdogReset = millis();
}

void checkWatchdog() {
  unsigned long now = millis();
  
  if (now - lastWatchdogReset > WATCHDOG_TIMEOUT) {
    Serial.println("\n[WATCHDOG] Timeout detected! Restarting...");
    Serial.flush();
    ESP.restart();
  }
}

// ========== LOW POWER MANAGEMENT ==========
void manageLowPowerMode() {
  // Instead of deep sleep (which resets everything), we use
  // non-blocking timing and a minimal loop delay
  
  // Calculate when next action is needed
  unsigned long now = millis();
  unsigned long nextAction = min(nextVoltageCheckTime, nextApiReportTime);
  
  // If no immediate action needed, we can use a longer delay
  if (now < nextAction) {
    unsigned long timeUntilNextAction = nextAction - now;
    
    // Use longer delay when possible (max 1 second to keep responsiveness)
    unsigned long sleepDelay = min(timeUntilNextAction, 1000UL);
    
    if (sleepDelay > 10) {  // Only if we have meaningful time
      // Turn off WiFi radio if not needed soon and not connected
      if (!wifiConnected && timeUntilNextAction > 5000) {
        WiFi.disconnect();
        WiFi.mode(WIFI_OFF);
        Serial.println("[Power] WiFi turned off to save power");
      }
      
      delay(sleepDelay);
    }
  }
}

// ========== STATUS PRINTING ==========
void printStatus() {
  static unsigned long lastPrint = 0;
  unsigned long now = millis();
  
  if (now - lastPrint >= 60000) {  // Print every minute
    lastPrint = now;
    
    Serial.println("\n=== STATUS REPORT ===");
    Serial.printf("Uptime: %lu minutes\n", now / 60000);
    
    const char* stateNames[] = {"BOOT", "WIFI_CONNECTING", "NORMAL", 
                               "OFFLINE_DETECTED", "POWER_CYCLING", "SAFETY_TIMEOUT"};
    Serial.printf("State: %s\n", stateNames[currentState]);
    
    Serial.printf("Server: %s\n", serverOnline ? "ONLINE" : "OFFLINE");
    Serial.printf("WiFi: %s\n", wifiConnected ? "CONNECTED" : "DISCONNECTED");
    Serial.printf("API Queue: %d/%d\n", queueSize, MAX_QUEUE_SIZE);
    Serial.printf("Offline count: %d/%d\n", offlineCount, OFFLINE_CONSECUTIVE_COUNT);
    Serial.printf("Last power cycle: %lu seconds ago\n", 
                  (lastPowerCycleTime > 0 ? (now - lastPowerCycleTime) / 1000 : 0));
    Serial.printf("Voltage threshold: %d\n", VOLTAGE_THRESHOLD);
    Serial.printf("Watchdog: %lu seconds since reset\n", (now - lastWatchdogReset) / 1000);
    
    unsigned long nextVolt = (nextVoltageCheckTime > now) ? (nextVoltageCheckTime - now) / 1000 : 0;
    unsigned long nextApi = (nextApiReportTime > now) ? (nextApiReportTime - now) / 1000 : 0;
    Serial.printf("Next voltage check: %lu seconds\n", nextVolt);
    Serial.printf("Next API report: %lu seconds\n", nextApi);
    
    Serial.println("=====================\n");
  }
}

// ========== MAIN LOOP ==========
void loop() {
  // Update LED patterns
  updateLED();
  
  // Check WiFi status periodically
  checkWiFi();
  
  // Check voltage at configured interval
  checkVoltage();
  
  // Process API reports at configured interval
  unsigned long now = millis();
  if (now >= nextApiReportTime) {
    nextApiReportTime = now + API_REPORT_INTERVAL;
    if (wifiConnected) {
      performApiReport(serverOnline);
    } else {
      queueApiRequest(serverOnline);
    }
  }
  
  // Process queued API requests if WiFi is available
  if (wifiConnected && queueSize > 0) {
    processApiQueue();
  }
  
  // Check watchdog
  checkWatchdog();
  
  // Print status periodically
  printStatus();
  
  // Manage low power mode (without deep sleep)
  manageLowPowerMode();
  
  resetWatchdog();
}
