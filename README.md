# ESP8266 Server Auto-Power Controller

This project uses a **NodeMCU ESP8266** to monitor server power status using a 0-25V DC analog sensor connected to the server's USB port. If the server loses power, the device automatically triggers a power button press via a relay to restore power. Designed for reliable server monitoring and automated recovery.

---

## ✨ Features

* **Direct Power Monitoring**
  Uses 0-25V DC analog sensor to detect actual server power status via USB port voltage
  No unreliable ping checks - detects power loss in real-time

* **Intelligent Power Detection**
  Configurable threshold detection with consecutive reading confirmation:
  - 5-second check interval
  - 3 consecutive offline readings required to trigger power cycle

* **Automated Power Recovery**
  When power loss is confirmed, automatically presses server power button via relay (500ms pulse)
  Configurable safety timeout between power cycles

* **API Health Reporting**
  Sends HTTPS POST requests to remote endpoint every 30 seconds with power status
  Failed API calls are queued and retried when WiFi reconnects
  Maximum 3 queued requests (oldest discarded when full)

* **Asynchronous & Non-Blocking**
  All operations use non-blocking delays for responsive performance
  Task queuing ensures operations are serialized and reliable

* **WiFi Resilience**
  Continues voltage monitoring during WiFi outages
  Automatic reconnection with status indication
  Queue system ensures no data loss during disconnections

* **Watchdog Protection**
  1-minute software watchdog timer for crash recovery
  Automatic ESP8266 restart if system hangs

* **Minimal LED Indication** (Designed for bedroom use)
  Fast blinking during WiFi connection
  LED on only during power cycling
  No annoying flickering during normal operation

* **Configurable Parameters**
  All timing, thresholds, and intervals easily adjustable
  Safe defaults for reliable operation

* **Verbose Serial Debugging**
  Detailed logging of all operations
  Periodic status reports for monitoring

* **Secure HTTPS Communication**
  Supports SSL/TLS using BearSSL with insecure mode for self-signed certificates

---

## 📡 How It Works

1. **ESP8266 boots** and connects to WiFi (waits for connection before proceeding)
2. **Reads analog voltage** every 5 seconds from server USB port via A0 pin
3. **Processes voltage readings** using configurable threshold (default: 100 = ~0.5V)
4. **Confirms offline status** after 3 consecutive offline readings (15 seconds total)
5. **Triggers power cycle** via relay on D1 pin (500ms pulse, LOW = ON)
6. **Reports to API** every 30 seconds regardless of voltage checks
7. **Manages WiFi disconnections** by queuing failed API calls
8. **Uses non-blocking timing** for all operations
9. **Maintains state** across operations (no deep sleep reset)
10. **Monitors watchdog** and auto-restarts if system hangs

---

## API Integration

This device is designed to work with the **[UPtime](https://github.com/FireFlyDeveloper/UPtime.git)** monitoring system:

> UPtime is a Node.js monitoring system that tracks device status in real-time and sends professional email alerts for offline events, boot attempts, and failures, with Philippine timezone timestamps and reliable retry notifications.

The API endpoint expects regular POST requests with the following JSON format:
```json
{
  "isOnline": true|false,
  "password": "FireFlyDeveloper2025"
}
```

API calls are made every 30 seconds, with state changes reported immediately when WiFi is available.

---

## 🛠️ Hardware Requirements

* NodeMCU ESP8266 (or any ESP8266 board with analog input)
* 0-25V DC Analog Voltage Sensor Module
* Single-channel relay module (for power button control)
* USB cable (to connect from server USB port to voltage sensor)
* Wires & connectors
* 5V power supply/charger for ESP8266

---

## 🔌 Hardware Connections

### ESP8266 Pinout:
```
D1        → Relay IN pin (LOW = Relay ON, HIGH = Relay OFF)
A0        → Voltage sensor output (0-25V sensor module output)
LED_BUILTIN → Status LED (GPIO2)
GND       → Sensor GND + Relay GND
VIN       → 5V charger/power supply
```

### Relay Connections:
```
ESP8266 D1 → Relay IN (signal)
Relay COM  → Server power button "press" wire
Relay NO   → Server power button "common" wire
Relay VCC  → 5V (from ESP8266 VIN or separate supply)
Relay GND  → GND
```

### Voltage Sensor Connections:
```
Server USB +5V (Red) → Voltage sensor VCC
Server USB GND (Black) → Voltage sensor GND
Voltage sensor OUT → ESP8266 A0
Voltage sensor GND → ESP8266 GND
```

### Server Power Button:
Connect relay contacts in parallel with existing power button (momentary switch).

---

## 🚀 Software Setup

### 1. Install Dependencies
- Install **ESP8266 board package** in Arduino IDE
- Required libraries (installed via Arduino Library Manager):
  - ESP8266WiFi
  - ESP8266HTTPClient
  - WiFiClientSecure

### 2. Configure Settings
Edit the following parameters at the top of the sketch:

```cpp
// WiFi Configuration
const char* WIFI_SSID = "main";
const char* WIFI_PASSWORD = "SaludesFam2024";

// API Configuration
const char* API_URL = "https://uptime-wtov.onrender.com/health";
const char* API_PASSWORD = "FireFlyDeveloper2025";

// Voltage threshold (0-1023, default 100 ≈ 0.5V)
const int VOLTAGE_THRESHOLD = 100;

// Timing parameters (all in milliseconds)
const unsigned long VOLTAGE_CHECK_INTERVAL = 5000;      // 5 seconds
const unsigned long API_REPORT_INTERVAL = 30000;        // 30 seconds
const unsigned long RELAY_PULSE_DURATION = 500;         // 500ms
const unsigned long SAFETY_TIMEOUT = 30000;             // 30 seconds between cycles

// Detection parameters
const int OFFLINE_CONSECUTIVE_COUNT = 3;  // 3 readings before triggering
const int MAX_QUEUE_SIZE = 3;             // Max queued API calls
```

### 3. Upload to ESP8266
- Connect ESP8266 via USB to computer
- Select board: "NodeMCU 1.0 (ESP-12E Module)"
- Set correct COM port
- Upload at 115200 baud
- Open Serial Monitor (115200 baud) to verify operation

---

## 🔧 Configuration Guide

### Voltage Threshold Calibration
1. Connect sensor to powered-on server USB port
2. Open Serial Monitor (115200 baud)
3. Note the analog readings (should be > 100 when server is on)
4. Adjust `VOLTAGE_THRESHOLD`:
   - Default: 100 (≈0.5V)
   - Increase if getting false offline detection
   - Decrease if not detecting offline state properly

### Timing Configuration
- `VOLTAGE_CHECK_INTERVAL`: How often to check voltage (5000ms = 5 seconds)
- `API_REPORT_INTERVAL`: How often to send API updates (30000ms = 30 seconds)
- `RELAY_PULSE_DURATION`: How long to press power button (500ms)
- `SAFETY_TIMEOUT`: Minimum time between power cycles (30000ms = 30 seconds)
- `OFFLINE_CONSECUTIVE_COUNT`: Number of offline readings before action (3)

### WiFi Configuration
- Device waits for WiFi connection on boot
- Continues operation during WiFi outages
- Reconnects automatically with visual indication
- Failed API calls are queued for retry

### Queue Configuration
- Maximum 3 queued API requests
- Oldest request discarded when queue is full
- Automatic processing when WiFi reconnects
- Retry every 30 seconds for failed calls

---

## 📈 Operation Flow

### Normal Operation:
1. WiFi connects on boot (LED fast blink during connection)
2. Voltage checks every 5 seconds
3. API reports every 30 seconds
4. LED remains OFF during normal operation
5. Watchdog timer reset with each successful operation

### Offline Detection:
1. Voltage reading below threshold
2. Counter increments (3 readings required = 15 seconds)
3. Safety timeout checked (30 seconds since last cycle)
4. Relay triggered for 500ms (LED ON during pulse)
5. API reports offline status
6. Counter reset

### WiFi Disconnection:
1. Voltage monitoring continues
2. API calls queued (max 3)
3. LED slow blink indicates WiFi disconnected
4. Automatic reconnection attempts
5. Queued API calls processed when WiFi returns

---

## 🚨 Troubleshooting

### Common Issues & Solutions:

1. **No Serial Output**
   - Verify baud rate: 115200
   - Check USB cable and COM port
   - Ensure ESP8266 is powered

2. **WiFi Connection Failed**
   - Verify SSID and password
   - Check router signal strength
   - Ensure WiFi network is 2.4GHz (ESP8266 doesn't support 5GHz)

3. **False Power Cycle Triggers**
   - Increase `VOLTAGE_THRESHOLD`
   - Increase `OFFLINE_CONSECUTIVE_COUNT`
   - Check sensor connections
   - Verify stable USB power from server

4. **Relay Not Triggering**
   - Verify D1 pin is configured correctly
   - Check relay module power (5V)
   - Test with multimeter for output
   - Remember: LOW = Relay ON

5. **API Connection Failures**
   - Check API URL and password
   - Verify server accepts HTTPS
   - Check for firewall restrictions
   - Monitor Serial for HTTP error codes

6. **Watchdog Resets**
   - Check for infinite loops
   - Verify non-blocking delays are working
   - Reduce operation time if taking too long

### Serial Debug Information:
Enable Serial Monitor (115200 baud) to see:
- WiFi connection status and IP address
- Voltage readings and threshold comparisons
- Offline detection counter
- API request successes/failures
- Queue status and operations
- State changes and timing
- Watchdog status
- Periodic status reports

---

## 🔒 Safety Notes

⚠️ **CRITICAL WARNINGS:**

1. **High Voltage Isolation**
   - Never connect mains voltage directly to ESP8266
   - Use properly rated relay for server power
   - Ensure relay contacts are isolated from control circuit

2. **Server Protection**
   - Test relay operation before connecting to server
   - Verify power button wiring is correct
   - Consider BIOS auto-power-on feature as backup
   - Add manual override capability

3. **Power Supply**
   - Use stable 5V power supply/charger
   - Ensure adequate current (ESP8266 + relay)
   - Consider using separate power for relay if needed

4. **USB Port Safety**
   - Ensure voltage sensor doesn't draw excessive current
   - Verify USB port can provide required power
   - Consider using powered USB hub if needed

### Recommended Safety Additions:
- Fuse on power input
- Surge protection
- Manual reset button
- Status indicator LEDs
- External watchdog timer

---

## 📄 License

This project is open-source. Modify and distribute as needed.

---

## 🤝 Contributing

Feel free to fork, modify, and submit pull requests. Key areas for improvement:
- Web configuration interface
- Additional sensor inputs (temperature, humidity)
- MQTT support for home automation integration
- LCD/OLED display for local status
- Mobile app for configuration and monitoring
- Battery backup system
- Multiple server support

---

## ⚡ Quick Start Checklist

- [ ] Assemble hardware: ESP8266, relay, voltage sensor
- [ ] Connect voltage sensor to server USB port
- [ ] Connect relay to server power button
- [ ] Connect ESP8266 to 5V power supply
- [ ] Update WiFi credentials in code
- [ ] Configure API URL and password
- [ ] Set voltage threshold based on calibration
- [ ] Upload sketch to ESP8266
- [ ] Open Serial Monitor (115200 baud) to verify
- [ ] Test normal operation (LED should be OFF)
- [ ] Simulate power loss (disconnect USB)
- [ ] Verify power cycle triggers after 15 seconds
- [ ] Confirm API reports are being sent
- [ ] Test WiFi disconnection/reconnection
- [ ] Verify queue operation during WiFi outage
- [ ] Final installation in server location

---

## 📊 Default Configuration Summary

| Parameter | Value | Description |
|-----------|-------|-------------|
| Voltage Check | 5 seconds | How often to read USB voltage |
| API Report | 30 seconds | How often to send status updates |
| Offline Threshold | 3 readings | Consecutive readings before power cycle |
| Relay Pulse | 500ms | How long to press power button |
| Safety Timeout | 30 seconds | Minimum time between power cycles |
| Voltage Threshold | 100 (0.5V) | Analog reading for offline detection |
| Queue Size | 3 requests | Maximum queued API calls |
| Watchdog | 60 seconds | Auto-restart if system hangs |
| LED Pattern | Minimal | LED only for WiFi/power cycling |

---

## 🔄 Update History

### Current Version Features:
- Non-blocking asynchronous operation
- Configurable timing and thresholds
- WiFi resilience with queue system
- Watchdog protection
- Minimal LED indication
- Verbose serial debugging
- Safety timeouts and limits
