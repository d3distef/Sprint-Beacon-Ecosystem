/*
  ================================================================================
  Sprint Timer - TAG BEACON (Start Gate)
  ESP32-S3 Zero/Mini - Firmware v2.0
  ================================================================================
  
  PURPOSE:
  This device acts as a START GATE for sprint timing. When a runner crosses
  the start line (detected by TOF sensor), it triggers the start of the sprint
  and sends the start signal to the paired Anchor beacon via UWB.
  
  COMMUNICATION ARCHITECTURE:
  - BLE: Used ONLY for pairing and configuration (shares UWB network info)
  - UWB: ALL timing data and communication with Anchor
  - NO ESP-NOW (removed completely)
  
  FEATURES:
  - BLE pairing with Anchor to exchange UWB network configuration
  - TOF sensor for optical start detection (no beam-break required)
  - UWB-only communication with Anchor for all timing data
  - Battery monitoring and power management
  - Deep sleep when not charging/in use
  - LED animations for visual feedback
  - OTA firmware updates
  
  HARDWARE:
  - ESP32-S3 Zero/Mini
  - VL53L1X TOF sensor (I2C) for start line detection
  - REYAX UWB module (UART) for distance measurement and communication
  - WS2812 8x8 LED matrix for status display
  - TP4056 charging circuit with battery monitoring
  - Momentary button for wake/sleep/OTA
  
  PAIRING PROCESS:
  1. User opens mobile app and connects to Anchor via BLE
  2. User taps "Add Tag" in app
  3. App scans for nearby Tags advertising via BLE
  4. User selects Tag from list
  5. App reads Tag's UWB address from BLE characteristic
  6. App writes Anchor's UWB network ID to Tag via BLE
  7. Tag configures UWB module with Anchor's network
  8. Tag saves config to NVS (non-volatile storage)
  9. Tag confirms pairing (green flash)
  10. All subsequent communication happens via UWB (BLE can be disabled)
  
  POWER STATES:
  - DEEP_SLEEP: Lowest power, 3.3V rail off, wake on button or charger
  - CHARGING: Plugged in, showing charge animation, 3.3V rail off
  - AWAKE_STANDBY: Ready for sprint, blue rotating row animation
  - AWAKE_RUNNING: Start triggered, green waves animation
  - PAIRING_MODE: BLE pairing active, purple breathing
  - OTA_MODE: Firmware update in progress, purple pulse
  
  ================================================================================
*/

// =================== Configuration Switches ===================
#define DIAG_VISUAL_ONLY       1     // 1 = LED-only error reporting (no Serial needed)
#define DEBUG_MODE             0     // 1 = enable extra debug Serial output

// =================== Library Includes ===================
#include <WiFi.h>             // For OTA only
#include <ArduinoOTA.h>       // Over-the-air firmware updates
#include <FastLED.h>          // LED control
#include <Preferences.h>      // Non-volatile storage (NVS)
#include <NimBLEDevice.h>     // Bluetooth Low Energy (pairing only)
#include <Wire.h>             // I2C communication
#include <SparkFun_VL53L1X.h> // TOF sensor library
#include "secrets.h"          // WiFi credentials for OTA

// =================== Hardware Pin Definitions ===================

// LED Matrix (WS2812 8x8 grid)
static const uint8_t PIN_WS2812    = 6;
static const int NUM_LEDS = 64;

// User Input
static const uint8_t PIN_OTA_BTN   = 8;     // Short <5s = sleep/wake, Long ≥5s = OTA

// Battery & Power Monitoring
const int PIN_BATT_SENSE   = 7;    // ADC: battery voltage (100K/100K divider)
const int PIN_3V_CTRL      = 5;    // 3.3V rail control (to converter EN pin)
const int PIN_CHARGE_SENSE = 4;    // Charger 5V detect (10K/10K divider)
const int PIN_STANDBY_SENSE = 10;  // TP4056 STDBY pin (HIGH when fully charged)

// I2C Bus (TOF Sensor)
#define I2C_SDA  1
#define I2C_SCL  2

// UART (UWB Module)
static const int UWB_RX  = 44;
static const int UWB_TX  = 43;
static const int UWB_BAUD = 115200;

// =================== LED Matrix Setup ===================
CRGB leds[NUM_LEDS];

// =================== Battery Monitoring Configuration ===================
const float ADC_REF_V = 3.3f;
const int   ADC_MAX = 4095;
const float BATT_DIVIDER_RATIO = 2.0f;     // 100K/100K voltage divider
const float BATT_CAL = 1.08f;              // Calibration factor
const float CHARGE_DIVIDER_RATIO = 2.0f;
const float CHARGE_DETECT_THRESH_V = 2.0f;
const float BATT_EMPTY_V = 3.20f;          // 0%
const float BATT_FULL_V = 4.20f;           // 100%

// =================== Power State Management ===================
enum class PowerState : uint8_t { 
  DEEP_SLEEP,     // Deep sleep, 3.3V rail off
  CHARGING,       // Plugged in, showing charge animation, rail off
  AWAKE_STANDBY,  // Awake and ready, blue rotating row
  AWAKE_RUNNING,  // Start triggered, green waves
  PAIRING_MODE,   // BLE pairing active, purple breathing
  OTA_MODE        // Firmware update in progress, purple pulse
};

static PowerState powerState = PowerState::AWAKE_STANDBY;
static uint8_t batteryPercent = 0;
static bool isCharging = false;
static bool isFullyCharged = false;
static bool rail3V3On = true;

// =================== TOF Sensor Setup ===================
SFEVL53L1X tof;

// TOF detection thresholds (in CENTIMETERS)
static const uint16_t MIN_ABS_CM   = 1;
static const uint16_t MIN_DROP_CM  = 10;     // ~body width
static const uint16_t BASELINE_MAX_CM = 183; // 6 feet

// Consecutive frame requirements for debouncing
static const uint8_t  BELOW_CONSEC_REQUIRED = 2;
static const uint8_t  ABOVE_CONSEC_REQUIRED = 2;

// Extended trigger window for narrow TOF beam
static const uint32_t TRIGGER_HOLD_MS = 500;

// TOF detection state variables
static uint16_t baseline_cm = 0;
static uint8_t belowConsec = 0;
static uint8_t aboveConsec = ABOVE_CONSEC_REQUIRED;
static bool    startArmed   = true;
static int32_t tof_cm = -1;
static uint32_t lastTriggerDetectionMs = 0;

// =================== Runtime State Variables ===================
static bool     prevStartTriggered = false;
static uint32_t tBoot            = 0;
static uint32_t tStartTriggeredTime = 0;
static int32_t  startTimestamp = -1;          // -1 = no start, >=0 = timestamp

// =================== UWB Configuration ===================
// These are loaded from NVS after pairing or set during pairing

// Our UWB address (unique per device, derived from MAC)
char MY_UWB_ADDRESS[16] = "TAG000000";  // Will be set from MAC address

// Anchor's UWB network configuration (received during pairing)
char ANCHOR_UWB_NETWORK[16] = "";       // e.g., "REYAX123"
char ANCHOR_UWB_ADDRESS[16] = "";       // e.g., "ANCH0001"
bool isPaired = false;                  // True if we have valid network config

// UWB distance tracking
static int32_t  uwb_distance_cm = -1;   // Current distance to Anchor in cm

// =================== BLE Configuration ===================
// BLE is used ONLY for pairing and configuration

// BLE Service and Characteristic UUIDs
#define BLE_SERVICE_UUID        "b8c7f3f4-4b9f-4a5b-9c39-36c6b4c7e0a1"
#define BLE_STATUS_UUID         "b8c7f3f4-4b9f-4a5b-9c39-36c6b4c7e0b2" // READ|NOTIFY
#define BLE_UWB_INFO_UUID       "b8c7f3f4-4b9f-4a5b-9c39-36c6b4c7e0c6" // READ (our UWB addr)
#define BLE_NETWORK_CFG_UUID    "b8c7f3f4-4b9f-4a5b-9c39-36c6b4c7e0c7" // WRITE (Anchor config)
#define BLE_CONTROL_UUID        "b8c7f3f4-4b9f-4a5b-9c39-36c6b4c7e0c5" // WRITE (commands)

// BLE objects
static NimBLEServer*         bleServer   = nullptr;
static NimBLEService*        bleService  = nullptr;
static NimBLECharacteristic* chStatus    = nullptr;
static NimBLECharacteristic* chUwbInfo   = nullptr;  // Advertise our UWB address
static NimBLECharacteristic* chNetworkCfg = nullptr; // Receive Anchor's UWB config
static NimBLECharacteristic* chControl   = nullptr;
static NimBLEAdvertising*    bleAdv      = nullptr;
static volatile bool         bleClientConnected = false;

// =================== Non-Volatile Storage ===================
Preferences prefs;

// =================== UWB UART Setup ===================
HardwareSerial UWB(1);

// =================== Debug Output Macro ===================
#if !DIAG_VISUAL_ONLY
  #define DBG(...)  Serial.printf(__VA_ARGS__)
#else
  #define DBG(...)
#endif

// ================================================================================
// BATTERY & POWER MANAGEMENT FUNCTIONS
// ================================================================================

/*
  Read battery voltage from ADC with voltage divider compensation
  Returns: Battery voltage in volts
*/
float readBatteryVoltage() {
  int raw = analogRead(PIN_BATT_SENSE);
  float v_pin = (raw * ADC_REF_V) / ADC_MAX;
  float v_batt = v_pin * BATT_DIVIDER_RATIO;
  v_batt *= BATT_CAL;
  return v_batt;
}

/*
  Convert battery voltage to percentage (0-100%)
  Uses linear interpolation between empty and full voltage
*/
uint8_t voltageToBatteryPercent(float v) {
  if (v <= BATT_EMPTY_V) return 0;
  if (v >= BATT_FULL_V) return 100;
  float pct = (v - BATT_EMPTY_V) / (BATT_FULL_V - BATT_EMPTY_V) * 100.0f;
  return (uint8_t)(constrain(pct, 0, 100) + 0.5f);
}

/*
  Check if charger is connected
  Returns: True if charger voltage detected
*/
bool checkCharging() {
  int raw = analogRead(PIN_CHARGE_SENSE);
  float v_pin = (raw * ADC_REF_V) / ADC_MAX;
  return (v_pin >= CHARGE_DETECT_THRESH_V);
}

/*
  Check if battery is fully charged
  Returns: True if TP4056 STDBY pin is HIGH
*/
bool checkFullyCharged() {
  return (digitalRead(PIN_STANDBY_SENSE) == HIGH);
}

/*
  Control the 3.3V rail to peripherals
  Manages I2C pins to prevent backfeeding when rail is off
*/
void set3V3Rail(bool on) {
  if (on) {
    digitalWrite(PIN_3V_CTRL, HIGH);  // Enable rail
    pinMode(I2C_SDA, INPUT);
    pinMode(I2C_SCL, INPUT);
  } else {
    digitalWrite(PIN_3V_CTRL, LOW);   // Disable rail
    pinMode(I2C_SDA, INPUT_PULLDOWN); // Prevent backfeeding
    pinMode(I2C_SCL, INPUT_PULLDOWN);
  }
  rail3V3On = on;
}

/*
  Enter deep sleep mode
  Configures wakeup sources (button or charger) and sleeps
  Device resets on wakeup
*/
void enterDeepSleep() {
  DBG("Entering deep sleep...\n");
  set3V3Rail(false);
  FastLED.clear();
  FastLED.show();
  
  // Configure wakeup sources
  esp_sleep_enable_ext0_wakeup((gpio_num_t)PIN_OTA_BTN, 0);  // Button LOW
  esp_sleep_enable_ext1_wakeup((1ULL << PIN_CHARGE_SENSE), ESP_EXT1_WAKEUP_ANY_HIGH);
  
  esp_deep_sleep_start();
}

// ================================================================================
// LED ANIMATION FUNCTIONS
// ================================================================================

/*
  Convert X,Y coordinates to LED index for 8x8 matrix
*/
inline uint16_t XY(uint8_t x, uint8_t y) { 
  return (uint16_t)y * 8u + x; 
}

/*
  Standby animation: Single rotating blue row
  Indicates device is awake and ready
*/
void ledsStandby(uint32_t now) {
  fill_solid(leds, NUM_LEDS, CRGB::Black);
  uint8_t row = (now / 250) % 8;
  const CRGB blue = CRGB(20, 100, 255);
  for (uint8_t x = 0; x < 8; ++x) {
    leds[XY(x, row)] = blue;
  }
}

/*
  Green waves animation: Sprint in progress
  Used when start has been triggered
*/
void ledsGreenWaves(uint32_t now) {
  fill_solid(leds, NUM_LEDS, CRGB::Black);
  for (uint8_t y = 0; y < 8; y++) {
    for (uint8_t x = 0; x < 8; x++) {
      uint8_t v = (sin8((x * 32) + (now / 3)) + sin8((y * 48) + (now / 4))) / 2;
      leds[XY(x, y)] = CRGB(0, v, 0);
    }
  }
}

/*
  OTA pulse animation: Firmware update in progress
  Slow breathing purple pulse
*/
void ledsOTAPulse(uint32_t now) {
  uint8_t v = beatsin8(12, 15, 180);
  CRGB purple = CRGB(v / 2, 0, v);
  fill_solid(leds, NUM_LEDS, purple);
}

/*
  Pairing mode animation: BLE pairing active
  Medium-speed purple breathing
*/
void ledsPairingMode(uint32_t now) {
  uint8_t v = beatsin8(20, 30, 200);
  CRGB purple = CRGB(v, 0, v);
  fill_solid(leds, NUM_LEDS, purple);
}

/*
  Charging rings animation: Battery charging progress
  Displays concentric rings based on battery percentage
*/
void ledsChargingRings(uint32_t now, uint8_t pct) {
  fill_solid(leds, NUM_LEDS, CRGB::Black);
  
  uint8_t targetRing = (pct * 8) / 100;
  if (targetRing > 7) targetRing = 7;
  
  uint32_t cycleMs = 2000;
  uint32_t stepMs = cycleMs / (targetRing + 1);
  uint8_t currentRing = ((now % cycleMs) / stepMs);
  if (currentRing > targetRing) currentRing = targetRing;
  
  const CRGB blue = CRGB(0, 100, 255);
  
  switch(currentRing) {
    case 0:  // Center 2x2
      for (uint8_t y = 3; y <= 4; y++)
        for (uint8_t x = 3; x <= 4; x++)
          leds[XY(x, y)] = blue;
      break;
    case 1:  // 4x4 hollow
      for (uint8_t x = 2; x <= 5; x++) { 
        leds[XY(x, 2)] = blue; 
        leds[XY(x, 5)] = blue; 
      }
      for (uint8_t y = 3; y <= 4; y++) { 
        leds[XY(2, y)] = blue; 
        leds[XY(5, y)] = blue; 
      }
      break;
    case 2:  // 6x6 hollow
      for (uint8_t x = 1; x <= 6; x++) { 
        leds[XY(x, 1)] = blue; 
        leds[XY(x, 6)] = blue; 
      }
      for (uint8_t y = 2; y <= 5; y++) { 
        leds[XY(1, y)] = blue; 
        leds[XY(6, y)] = blue; 
      }
      break;
    case 3:  // 8x8 outer edge
      for (uint8_t x = 0; x < 8; x++) { 
        leds[XY(x, 0)] = blue; 
        leds[XY(x, 7)] = blue; 
      }
      for (uint8_t y = 1; y < 7; y++) { 
        leds[XY(0, y)] = blue; 
        leds[XY(7, y)] = blue; 
      }
      break;
    default:  // High percentage - show all rings
      for (uint8_t i = 0; i <= 3; i++) {
        switch(i) {
          case 0:
            for (uint8_t y = 3; y <= 4; y++)
              for (uint8_t x = 3; x <= 4; x++)
                leds[XY(x, y)] = blue;
            break;
          case 1:
            for (uint8_t x = 2; x <= 5; x++) { 
              leds[XY(x, 2)] = blue; leds[XY(x, 5)] = blue; 
            }
            for (uint8_t y = 3; y <= 4; y++) { 
              leds[XY(2, y)] = blue; leds[XY(5, y)] = blue; 
            }
            break;
          case 2:
            for (uint8_t x = 1; x <= 6; x++) { 
              leds[XY(x, 1)] = blue; leds[XY(x, 6)] = blue; 
            }
            for (uint8_t y = 2; y <= 5; y++) { 
              leds[XY(1, y)] = blue; leds[XY(6, y)] = blue; 
            }
            break;
          case 3:
            for (uint8_t x = 0; x < 8; x++) { 
              leds[XY(x, 0)] = blue; leds[XY(x, 7)] = blue; 
            }
            for (uint8_t y = 1; y < 7; y++) { 
              leds[XY(0, y)] = blue; leds[XY(7, y)] = blue; 
            }
            break;
        }
      }
      break;
  }
}

/*
  Fully charged animation: Slow blue pulse
  Indicates battery is at 100%
*/
void ledsFullyCharged(uint32_t now) {
  uint8_t v = beatsin8(8, 30, 200);
  CRGB blue = CRGB(0, v / 2, v);
  fill_solid(leds, NUM_LEDS, blue);
}

// ================================================================================
// BLE CALLBACKS AND FUNCTIONS
// ================================================================================

/*
  BLE Server Callbacks
  Handles connection and disconnection events
*/
class SvrCB : public NimBLEServerCallbacks {
 public:
  void onConnect(NimBLEServer* s) { 
    (void)s; 
    bleClientConnected = true; 
    DBG("[BLE] Client connected\n");
  }
  
  void onConnect(NimBLEServer* s, ble_gap_conn_desc* /*desc*/) { 
    onConnect(s); 
  }
  
  void onDisconnect(NimBLEServer* s) { 
    bleClientConnected = false; 
    DBG("[BLE] Client disconnected\n");
    if (s) s->startAdvertising(); 
  }
};

/*
  Network Configuration Characteristic Callback
  Receives Anchor's UWB network configuration during pairing
  
  Expected format: JSON string
  {
    "network_id": "REYAX123",
    "anchor_address": "ANCH0001"
  }
*/
class NetworkCfgCB : public NimBLECharacteristicCallbacks {
public:
  void onWrite(NimBLECharacteristic* c) { handleWrite(c); }
  void onWrite(NimBLECharacteristic* c, NimBLEConnInfo& /*ci*/) { handleWrite(c); }
  
private:
  void handleWrite(NimBLECharacteristic* c) {
    if (!c) return;
    
    std::string v = c->getValue();
    DBG("[BLE] Network config write (%u bytes): %s\n", (unsigned)v.size(), v.c_str());
    
    // Parse JSON to extract network_id and anchor_address
    // Simple parsing (production code should use proper JSON library)
    
    // Look for "network_id":"..."
    size_t netPos = v.find("\"network_id\"");
    if (netPos != std::string::npos) {
      size_t colonPos = v.find(":", netPos);
      size_t firstQuote = v.find("\"", colonPos);
      size_t secondQuote = v.find("\"", firstQuote + 1);
      
      if (firstQuote != std::string::npos && secondQuote != std::string::npos) {
        std::string netId = v.substr(firstQuote + 1, secondQuote - firstQuote - 1);
        strncpy(ANCHOR_UWB_NETWORK, netId.c_str(), sizeof(ANCHOR_UWB_NETWORK) - 1);
        ANCHOR_UWB_NETWORK[sizeof(ANCHOR_UWB_NETWORK) - 1] = '\0';
      }
    }
    
    // Look for "anchor_address":"..."
    size_t addrPos = v.find("\"anchor_address\"");
    if (addrPos != std::string::npos) {
      size_t colonPos = v.find(":", addrPos);
      size_t firstQuote = v.find("\"", colonPos);
      size_t secondQuote = v.find("\"", firstQuote + 1);
      
      if (firstQuote != std::string::npos && secondQuote != std::string::npos) {
        std::string addr = v.substr(firstQuote + 1, secondQuote - firstQuote - 1);
        strncpy(ANCHOR_UWB_ADDRESS, addr.c_str(), sizeof(ANCHOR_UWB_ADDRESS) - 1);
        ANCHOR_UWB_ADDRESS[sizeof(ANCHOR_UWB_ADDRESS) - 1] = '\0';
      }
    }
    
    DBG("[BLE] Received network config:\n");
    DBG("      Network ID: %s\n", ANCHOR_UWB_NETWORK);
    DBG("      Anchor Address: %s\n", ANCHOR_UWB_ADDRESS);
    
    // Save to NVS
    prefs.begin("sprint-tag", false);
    prefs.putString("uwb_network", ANCHOR_UWB_NETWORK);
    prefs.putString("anchor_addr", ANCHOR_UWB_ADDRESS);
    prefs.putBool("paired", true);
    prefs.end();
    
    isPaired = true;
    
    // Configure UWB module with new network settings
    configureUWB();
    
    // Confirm pairing with green flash
    fill_solid(leds, NUM_LEDS, CRGB::Green);
    FastLED.show();
    delay(500);
    
    // Exit pairing mode
    powerState = PowerState::AWAKE_STANDBY;
    
    // Send confirmation via BLE
    if (chStatus) {
      char js[256];
      snprintf(js, sizeof(js), 
               "{\"paired\":true,\"network\":\"%s\",\"anchor\":\"%s\"}",
               ANCHOR_UWB_NETWORK, ANCHOR_UWB_ADDRESS);
      chStatus->setValue((uint8_t*)js, strlen(js));
      if (bleClientConnected) chStatus->notify();
    }
    
    DBG("[BLE] Pairing complete!\n");
  }
};

/*
  Control Characteristic Callback
  Handles commands from mobile app
*/
class CtrlCB : public NimBLECharacteristicCallbacks {
public:
  void onWrite(NimBLECharacteristic* c) { handleWrite(c); }
  void onWrite(NimBLECharacteristic* c, NimBLEConnInfo& /*ci*/) { handleWrite(c); }
  
private:
  void handleWrite(NimBLECharacteristic* c) {
    if (!c) return;
    
    std::string v = c->getValue();
    DBG("[BLE] Control write: %s\n", v.c_str());
    
    // Enter pairing mode
    if (v.find("\"enter_pairing_mode\":true") != std::string::npos ||
        v.find("\"pairing\":true") != std::string::npos) {
      DBG("[BLE] Entering pairing mode\n");
      powerState = PowerState::PAIRING_MODE;
      
      if (chStatus) {
        const char* ack = "{\"ack\":\"entering_pairing_mode\"}";
        chStatus->setValue((uint8_t*)ack, strlen(ack));
        if (bleClientConnected) chStatus->notify();
      }
    }
    // Unpair
    else if (v.find("\"unpair\":true") != std::string::npos) {
      DBG("[BLE] Unpairing\n");
      
      memset(ANCHOR_UWB_NETWORK, 0, sizeof(ANCHOR_UWB_NETWORK));
      memset(ANCHOR_UWB_ADDRESS, 0, sizeof(ANCHOR_UWB_ADDRESS));
      isPaired = false;
      
      prefs.begin("sprint-tag", false);
      prefs.remove("uwb_network");
      prefs.remove("anchor_addr");
      prefs.putBool("paired", false);
      prefs.end();
      
      if (chStatus) {
        const char* ack = "{\"ack\":\"unpaired\"}";
        chStatus->setValue((uint8_t*)ack, strlen(ack));
        if (bleClientConnected) chStatus->notify();
      }
      
      DBG("[BLE] Unpaired\n");
    }
  }
};

/*
  Start BLE server and advertising
  BLE is used ONLY for pairing - advertises our UWB address
*/
static void startBLE() {
  DBG("[BLE] Initializing...\n");
  
  // Generate unique BLE name from MAC address
  uint8_t mac[6];
  esp_efuse_mac_get_default(mac);
  char bleName[20];
  snprintf(bleName, sizeof(bleName), "Tag-%02X%02X%02X", mac[3], mac[4], mac[5]);
  
  NimBLEDevice::init(bleName);
  
  bleServer = NimBLEDevice::createServer();
  bleServer->setCallbacks(new SvrCB());
  
  bleService = bleServer->createService(BLE_SERVICE_UUID);
  
  // Status characteristic (READ | NOTIFY)
  chStatus = bleService->createCharacteristic(
    BLE_STATUS_UUID,  
    NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY
  );
  
  // UWB Info characteristic (READ) - advertises our UWB address
  chUwbInfo = bleService->createCharacteristic(
    BLE_UWB_INFO_UUID, 
    NIMBLE_PROPERTY::READ
  );
  
  // Set our UWB address in this characteristic
  char uwbInfo[64];
  snprintf(uwbInfo, sizeof(uwbInfo), 
           "{\"uwb_address\":\"%s\",\"device_type\":\"tag\"}", 
           MY_UWB_ADDRESS);
  chUwbInfo->setValue((uint8_t*)uwbInfo, strlen(uwbInfo));
  
  // Network Config characteristic (WRITE) - receives Anchor's UWB config
  chNetworkCfg = bleService->createCharacteristic(
    BLE_NETWORK_CFG_UUID, 
    NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR
  );
  chNetworkCfg->setCallbacks(new NetworkCfgCB());
  
  // Control characteristic (WRITE)
  chControl = bleService->createCharacteristic(
    BLE_CONTROL_UUID, 
    NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR
  );
  chControl->setCallbacks(new CtrlCB());
  
  // Set initial status
  char status[128];
  snprintf(status, sizeof(status), 
           "{\"paired\":%s,\"battery\":%d,\"uwb_address\":\"%s\"}", 
           isPaired ? "true" : "false", batteryPercent, MY_UWB_ADDRESS);
  chStatus->setValue((uint8_t*)status, strlen(status));
  
  bleService->start();
  
  // Start advertising
  bleAdv = NimBLEDevice::getAdvertising();
  bleAdv->addServiceUUID(BLE_SERVICE_UUID);
  
  // Add UWB address to manufacturer data for easy scanning
  uint8_t mfgData[16];
  mfgData[0] = 0xA1;  // Sprint Timer manufacturer ID
  mfgData[1] = 0x01;  // Device type: Tag
  memcpy(mfgData + 2, MY_UWB_ADDRESS, min(14, (int)strlen(MY_UWB_ADDRESS)));
  bleAdv->setManufacturerData(std::string((char*)mfgData, 16));
  
  bleAdv->start();
  
  DBG("[BLE] Started advertising as '%s'\n", bleName);
  DBG("[BLE] UWB Address: %s\n", MY_UWB_ADDRESS);
}

/*
  Update BLE status characteristic
  Sends current device status to connected app
*/
static void updateBLEStatus() {
  if (!chStatus || !bleClientConnected) return;
  
  char js[256];
  snprintf(js, sizeof(js),
    "{\"paired\":%s,\"battery\":%d,\"charging\":%s,\"power_state\":%d,\"start_armed\":%s,\"uwb_address\":\"%s\"}",
    isPaired ? "true" : "false",
    batteryPercent,
    isCharging ? "true" : "false",
    (int)powerState,
    startArmed ? "true" : "false",
    MY_UWB_ADDRESS
  );
  
  chStatus->setValue((uint8_t*)js, strlen(js));
  chStatus->notify();
}

// ================================================================================
// UWB MODULE FUNCTIONS
// ================================================================================

/*
  Send AT command to UWB module
  
  Returns: True if command succeeded, false on timeout or error
*/
bool sendAT(const String& cmd, uint32_t timeoutMs = 2000, bool waitForOK = true) {
  DBG("[UWB] >> %s\n", cmd.c_str());
  
  UWB.print(cmd);
  UWB.print("\r\n");
  
  uint32_t start = millis();
  String line;
  
  while (millis() - start < timeoutMs) {
    while (UWB.available()) {
      char c = (char)UWB.read();
      
      if (c == '\n') {
        line.trim();
        if (line.length()) {
          DBG("[UWB] << %s\n", line.c_str());
        }
        
        if (line.startsWith("+READY")) return true;
        
        if (waitForOK) {
          if (line.startsWith("+OK"))   return true;
          if (line.startsWith("+ERR"))  return false;
        } else {
          return true;
        }
        
        line = "";
      } else if (c != '\r') {
        line += c;
      }
    }
  }
  
  DBG("[UWB] Timeout\n");
  return false;
}

/*
  Synchronize with UWB module
  Verifies module is present and responsive
*/
bool uwbSync() {
  delay(500);
  while (UWB.available()) UWB.read();
  
  for (int i = 0; i < 5; ++i) {
    if (sendAT("AT", 500)) {
      DBG("[UWB] Module responded\n");
      return true;
    }
    delay(300);
  }
  
  DBG("[UWB] ERROR: No response\n");
  return false;
}

/*
  Configure UWB module with current network settings
  Called after pairing or on boot if already paired
*/
static void configureUWB() {
  DBG("[UWB] Configuring module...\n");
  DBG("      Mode: TAG\n");
  DBG("      Network: %s\n", ANCHOR_UWB_NETWORK);
  DBG("      Address: %s\n", MY_UWB_ADDRESS);
  
  // Flush buffer
  while (UWB.available()) UWB.read();
  
  // Configure UWB module
  sendAT("AT");
  sendAT("AT+MODE=0");  // 0 = TAG mode
  
  // Set network ID (must match Anchor)
  String netCmd = "AT+NETWORKID=" + String(ANCHOR_UWB_NETWORK);
  sendAT(netCmd.c_str());
  
  // Set our address
  String addrCmd = "AT+ADDRESS=" + String(MY_UWB_ADDRESS);
  sendAT(addrCmd.c_str());
  
  sendAT("AT+RSSI=1");  // Enable RSSI
  
  DBG("[UWB] Configuration complete\n");
}

/*
  Poll UWB module - send data and parse responses
  
  This is the ONLY communication method with Anchor.
  Sends: Battery, distance, start events
  Receives: Distance measurements, acknowledgments
*/
static void pollUWB() {
  static uint32_t lastSend = 0;
  static String line;
  
  uint32_t now = millis();
  
  // ===== Send data packet every 300ms =====
  if (isPaired && now - lastSend >= 300) {
    lastSend = now;
    
    // Build data packet as hex string
    // Format: Battery(1) | Distance(2) | StartAge(4)
    char hexData[32];
    
    // Battery percentage (1 byte)
    sprintf(hexData, "%02X", batteryPercent);
    
    // Distance in cm (2 bytes, big-endian)
    uint16_t dist = (uwb_distance_cm > 0 && uwb_distance_cm < 65535) ? 
                    (uint16_t)uwb_distance_cm : 0xFFFF;
    sprintf(hexData + 2, "%04X", dist);
    
    // Start age in ms (4 bytes, big-endian)
    int32_t startAge = -1;
    if (startTimestamp >= 0) {
      startAge = (int32_t)(now - (uint32_t)startTimestamp);
      // Clear timestamp after transmission
      startTimestamp = -1;
      DBG("[UWB] Sent START age=%ld ms\n", (long)startAge);
    }
    sprintf(hexData + 6, "%08X", (uint32_t)startAge);
    
    // Send to Anchor via UWB AT command
    char cmd[128];
    sprintf(cmd, "AT+TAG_SEND=%s,7,%s", ANCHOR_UWB_ADDRESS, hexData);
    UWB.print(cmd);
    UWB.print("\r\n");
  }
  
  // ===== Parse responses from Anchor =====
  while (UWB.available()) {
    char c = (char)UWB.read();
    
    if (c == '\r' || c == '\n') {
      if (line.length() > 0) {
        DBG("[UWB] << %s\n", line.c_str());
        
        // Parse distance from "+TAG_RCV=..." responses
        int idx = line.indexOf("+TAG_RCV=");
        if (idx >= 0) {
          int cmPos = line.indexOf(" cm", idx);
          if (cmPos > idx) {
            int j = cmPos - 1;
            while (j >= 0 && isdigit(line[j])) j--;
            String num = line.substring(j + 1, cmPos);
            int val = num.toInt();
            
            if (val > 0 && val < 65535) {
              uwb_distance_cm = (int32_t)val;
              DBG("[UWB] Distance = %ld cm\n", (long)uwb_distance_cm);
            }
          }
        }
      }
      line = "";
    } else {
      if (line.length() < 120) line += c;
    }
  }
}

// ================================================================================
// OTA FUNCTIONS
// ================================================================================

static const char* OTA_HOSTNAME = "sprint-tag";
static const char* OTA_PASSWORD = "U";

/*
  Start WiFi and OTA for firmware updates
  Disables BLE and UWB during OTA
*/
void startWiFiAndOTA() {
  DBG("[OTA] Starting...\n");
  
  // Stop BLE (not needed during OTA)
  if (bleServer) {
    bleServer->stopAdvertising();
    NimBLEDevice::deinit(false);
  }
  
  WiFi.mode(WIFI_STA);
  WiFi.persistent(false);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  
  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 8000) {
    delay(100);
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    DBG("[OTA] Connected to WiFi\n");
    DBG("[OTA] IP: %s\n", WiFi.localIP().toString().c_str());
  }
  
  ArduinoOTA.setHostname(OTA_HOSTNAME);
  ArduinoOTA.setPassword(OTA_PASSWORD);
  ArduinoOTA.onStart([](){
    fill_solid(leds, NUM_LEDS, CRGB::Blue);
    FastLED.show();
  });
  ArduinoOTA.begin();
  
  powerState = PowerState::OTA_MODE;
  fill_solid(leds, NUM_LEDS, CRGB::Blue);
  FastLED.show();
  
  DBG("[OTA] Ready\n");
}

// ================================================================================
// SERIAL INITIALIZATION
// ================================================================================

static void initSerial() {
#if !DIAG_VISUAL_ONLY
  Serial.begin(115200);
  uint32_t t0 = millis();
  while (!Serial && millis() - t0 < 1500) delay(10);
  Serial.println("\n\n[SERIAL] Ready\n");
#endif
}

// ================================================================================
// SETUP - ONE-TIME INITIALIZATION
// ================================================================================

void setup() {
  // Initialize Serial
  initSerial();
  
  DBG("\n================================================================================\n");
  DBG("Sprint Timer - TAG BEACON v2.0 (UWB Communication Only)\n");
  DBG("================================================================================\n\n");
  
  // Configure GPIO
  pinMode(PIN_3V_CTRL, OUTPUT);
  pinMode(PIN_BATT_SENSE, INPUT);
  pinMode(PIN_CHARGE_SENSE, INPUT);
  pinMode(PIN_STANDBY_SENSE, INPUT);
  pinMode(PIN_OTA_BTN, INPUT_PULLUP);
  analogSetAttenuation(ADC_11db);
  
  // Check wakeup cause
  esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();
  bool woke_from_button = (wakeup_reason == ESP_SLEEP_WAKEUP_EXT0);
  bool woke_from_charger = (wakeup_reason == ESP_SLEEP_WAKEUP_EXT1);
  
  DBG("[BOOT] Wakeup: %s\n", 
      woke_from_button ? "BUTTON" : 
      woke_from_charger ? "CHARGER" : "POWER-ON");
  
  // Read battery status
  float battV = readBatteryVoltage();
  batteryPercent = voltageToBatteryPercent(battV);
  isCharging = checkCharging();
  isFullyCharged = checkFullyCharged();
  
  DBG("[POWER] Battery: %.2fV (%d%%) | Charging: %s\n", 
      battV, batteryPercent, isCharging ? "YES" : "NO");
  
  // Initialize FastLED
  FastLED.addLeds<WS2812B, PIN_WS2812, GRB>(leds, NUM_LEDS);
  FastLED.setDither(false);
  FastLED.setBrightness(200);
  FastLED.clear();
  FastLED.show();
  
  // Generate unique UWB address from MAC
  uint8_t mac[6];
  esp_efuse_mac_get_default(mac);
  snprintf(MY_UWB_ADDRESS, sizeof(MY_UWB_ADDRESS), 
           "TAG%02X%02X%02X", mac[3], mac[4], mac[5]);
  
  DBG("[UWB] My Address: %s\n", MY_UWB_ADDRESS);
  
  // Load pairing data from NVS
  prefs.begin("sprint-tag", true);
  isPaired = prefs.getBool("paired", false);
  
  if (isPaired) {
    String net = prefs.getString("uwb_network", "");
    String addr = prefs.getString("anchor_addr", "");
    
    strncpy(ANCHOR_UWB_NETWORK, net.c_str(), sizeof(ANCHOR_UWB_NETWORK) - 1);
    strncpy(ANCHOR_UWB_ADDRESS, addr.c_str(), sizeof(ANCHOR_UWB_ADDRESS) - 1);
    
    DBG("[PAIRING] Loaded from NVS:\n");
    DBG("          Network: %s\n", ANCHOR_UWB_NETWORK);
    DBG("          Anchor: %s\n", ANCHOR_UWB_ADDRESS);
  } else {
    DBG("[PAIRING] Not paired yet\n");
  }
  prefs.end();
  
  // Determine boot mode
  bool bootOTA = false;
  uint32_t btnPressStart = millis();
  
  if (digitalRead(PIN_OTA_BTN) == LOW) {
    while (digitalRead(PIN_OTA_BTN) == LOW && (millis() - btnPressStart) < 5500) {
      delay(10);
    }
    uint32_t pressDuration = millis() - btnPressStart;
    
    if (pressDuration >= 5000) {
      bootOTA = true;
      DBG("[BOOT] Button ≥5s -> OTA mode\n");
    } else {
      if (woke_from_button) {
        DBG("[BOOT] Waking from sleep\n");
        set3V3Rail(true);
        powerState = PowerState::AWAKE_STANDBY;
      } else {
        DBG("[BOOT] Going to sleep\n");
        enterDeepSleep();
      }
    }
  } else if (woke_from_charger || isCharging) {
    DBG("[BOOT] Charger detected -> charging mode\n");
    set3V3Rail(false);
    powerState = PowerState::CHARGING;
  } else {
    DBG("[BOOT] Normal boot -> standby\n");
    set3V3Rail(true);
    powerState = PowerState::AWAKE_STANDBY;
  }
  
  // Enter OTA mode if requested
  if (bootOTA) {
    set3V3Rail(true);
    startWiFiAndOTA();
    return;
  }
  
  // Initialize peripherals if rail is ON
  if (rail3V3On) {
    // TOF Sensor
    Wire.begin(I2C_SDA, I2C_SCL);
    DBG("[TOF] Initializing...\n");
    
    if (tof.begin() != 0) {
      DBG("[TOF] ERROR: Not detected\n");
      fill_solid(leds, NUM_LEDS, CRGB::Red);
      FastLED.show();
      delay(1000);
    } else {
      tof.startRanging();
      tof.setDistanceModeShort();
      tof.setTimingBudgetInMs(20);
      DBG("[TOF] Ready\n");
    }
    
    // UWB Module
    UWB.begin(UWB_BAUD, SERIAL_8N1, UWB_RX, UWB_TX);
    
    if (!uwbSync()) {
      DBG("[UWB] ERROR: No response\n");
    } else {
      if (isPaired) {
        // Configure with saved network settings
        configureUWB();
      } else {
        // Default config (will be reconfigured after pairing)
        sendAT("AT+MODE=0");
        DBG("[UWB] Default config (not paired)\n");
      }
    }
    
    // BLE for pairing
    startBLE();
    
    // Boot complete indicator
    fill_solid(leds, NUM_LEDS, CRGB::White);
    FastLED.show();
    delay(100);
  }
  
  // Initialize state
  tBoot = millis();
  startArmed = true;
  prevStartTriggered = false;
  
  DBG("\n[BOOT] Setup complete. State: %d\n\n", (int)powerState);
}

// ================================================================================
// LOOP - MAIN PROGRAM LOOP
// ================================================================================

void loop() {
  uint32_t now = millis();
  
  // ========================================================================
  // OTA MODE
  // ========================================================================
  if (powerState == PowerState::OTA_MODE) {
    ArduinoOTA.handle();
    ledsOTAPulse(now);
    FastLED.show();
    delay(10);
    return;
  }
  
  // ========================================================================
  // CHARGING MODE
  // ========================================================================
  if (powerState == PowerState::CHARGING) {
    // Update battery status
    static uint32_t lastBattUpdate = 0;
    if (now - lastBattUpdate >= 1000) {
      lastBattUpdate = now;
      float battV = readBatteryVoltage();
      batteryPercent = voltageToBatteryPercent(battV);
      isCharging = checkCharging();
      isFullyCharged = checkFullyCharged();
      updateBLEStatus();
    }
    
    // Button handling
    static uint32_t btnPressStart = 0;
    static bool btnWasPressed = false;
    bool btnPressed = (digitalRead(PIN_OTA_BTN) == LOW);
    
    if (btnPressed && !btnWasPressed) {
      btnPressStart = now;
    } else if (btnPressed && btnWasPressed) {
      if (now - btnPressStart >= 5000) {
        DBG("[BUTTON] ≥5s -> OTA\n");
        set3V3Rail(true);
        startWiFiAndOTA();
        return;
      }
    } else if (!btnPressed && btnWasPressed) {
      if (now - btnPressStart < 5000) {
        DBG("[BUTTON] <5s -> wake\n");
        set3V3Rail(true);
        powerState = PowerState::AWAKE_STANDBY;
        
        // Re-init peripherals
        Wire.begin(I2C_SDA, I2C_SCL);
        if (tof.begin() == 0) {
          tof.startRanging();
          tof.setDistanceModeShort();
          tof.setTimingBudgetInMs(20);
        }
        
        UWB.begin(UWB_BAUD, SERIAL_8N1, UWB_RX, UWB_TX);
        if (uwbSync() && isPaired) {
          configureUWB();
        }
        
        startArmed = true;
        baseline_cm = 0;
        return;
      }
    }
    btnWasPressed = btnPressed;
    
    // Check charger disconnected
    if (!isCharging) {
      DBG("[CHARGING] Disconnected -> sleep\n");
      enterDeepSleep();
    }
    
    // Show animation
    if (isFullyCharged) {
      ledsFullyCharged(now);
    } else {
      ledsChargingRings(now, batteryPercent);
    }
    
    FastLED.show();
    delay(20);
    return;
  }
  
  // ========================================================================
  // PAIRING MODE
  // ========================================================================
  if (powerState == PowerState::PAIRING_MODE) {
    ledsPairingMode(now);
    FastLED.show();
    
    static uint32_t lastBLEUpdate = 0;
    if (now - lastBLEUpdate >= 1000) {
      lastBLEUpdate = now;
      updateBLEStatus();
    }
    
    delay(20);
    return;
  }
  
  // ========================================================================
  // AWAKE MODES - Normal operation
  // ========================================================================
  
  // Update battery periodically
  static uint32_t lastBattUpdate = 0;
  if (now - lastBattUpdate >= 5000) {
    lastBattUpdate = now;
    float battV = readBatteryVoltage();
    batteryPercent = voltageToBatteryPercent(battV);
    isCharging = checkCharging();
    isFullyCharged = checkFullyCharged();
    updateBLEStatus();
  }
  
  // Button handling
  static uint32_t btnPressStart = 0;
  static bool btnWasPressed = false;
  bool btnPressed = (digitalRead(PIN_OTA_BTN) == LOW);
  
  if (btnPressed && !btnWasPressed) {
    btnPressStart = now;
  } else if (btnPressed && btnWasPressed) {
    if (now - btnPressStart >= 5000) {
      DBG("[BUTTON] ≥5s -> OTA\n");
      startWiFiAndOTA();
      return;
    }
  } else if (!btnPressed && btnWasPressed) {
    if (now - btnPressStart < 5000) {
      DBG("[BUTTON] <5s -> sleep\n");
      enterDeepSleep();
    }
  }
  btnWasPressed = btnPressed;
  
  // TOF reading
  if (tof.checkForDataReady()) {
    int dist_mm = tof.getDistance();
    tof.clearInterrupt();
    tof_cm = (int32_t)(dist_mm / 10);
  }
  
  // UWB communication (all timing data via UWB)
  pollUWB();
  
  // ========================================================================
  // START DETECTION LOGIC
  // ========================================================================
  bool startTriggered = false;
  
  if (tof_cm > 0) {
    uint16_t cur_cm = (uint16_t)tof_cm;
    if (cur_cm > BASELINE_MAX_CM) cur_cm = BASELINE_MAX_CM;
    
    if (baseline_cm == 0) {
      baseline_cm = cur_cm;
      DBG("[TOF] Baseline: %d cm\n", baseline_cm);
    }
    
    const uint16_t drop_cm = (baseline_cm > cur_cm) ? (baseline_cm - cur_cm) : 0;
    
    // Check for significant drop
    if (cur_cm >= MIN_ABS_CM && drop_cm >= MIN_DROP_CM) {
      if (startArmed) {
        belowConsec++;
        lastTriggerDetectionMs = now;
        
        if (belowConsec >= BELOW_CONSEC_REQUIRED) {
          // START TRIGGERED
          startTriggered = true;
          startArmed = false;
          aboveConsec = 0;
          powerState = PowerState::AWAKE_RUNNING;
          DBG("[START] TRIGGERED! drop=%d cm\n", drop_cm);
        }
      } else {
        lastTriggerDetectionMs = now;
      }
    } else {
      if (belowConsec > 0) belowConsec--;
      
      // Re-arm logic
      if (!startArmed) {
        bool inHoldWindow = (now - lastTriggerDetectionMs) < TRIGGER_HOLD_MS;
        
        if (!inHoldWindow) {
          aboveConsec++;
          if (aboveConsec >= ABOVE_CONSEC_REQUIRED) {
            startArmed = true;
            belowConsec = 0;
            baseline_cm = cur_cm;
            
            if (powerState == PowerState::AWAKE_RUNNING) {
              powerState = PowerState::AWAKE_STANDBY;
            }
            DBG("[START] Re-armed\n");
          }
        } else {
          aboveConsec = 0;
        }
      }
    }
  }
  
  // Handle start edge
  if (startTriggered && !prevStartTriggered) {
    tStartTriggeredTime = now;
    startTimestamp = now;  // Will be sent via UWB in next poll
    
    DBG("\n========================================\n");
    DBG("[START] Runner crossed start line!\n");
    DBG("[START] TOF: %ld cm | UWB dist: %ld cm\n", 
        (long)tof_cm, (long)uwb_distance_cm);
    DBG("[START] Timestamp: %lu ms\n", (unsigned long)startTimestamp);
    DBG("========================================\n\n");
  }
  
  if (startArmed && prevStartTriggered) {
    DBG("[START] Re-armed\n");
  }
  
  prevStartTriggered = startTriggered;
  
  // ========================================================================
  // LED DISPLAY
  // ========================================================================
  switch (powerState) {
    case PowerState::AWAKE_STANDBY:
      ledsStandby(now);
      break;
    case PowerState::AWAKE_RUNNING:
      ledsGreenWaves(now);
      break;
    default:
      ledsStandby(now);
      break;
  }
  
  FastLED.show();
  
  // ========================================================================
  // DEBUG OUTPUT
  // ========================================================================
  static uint32_t lastDbg = 0;
  if (now - lastDbg >= 200) {
    lastDbg = now;
    DBG("[DBG] t=%lu state=%d paired=%d armed=%d tof=%ld uwb=%ld batt=%d%% below=%u above=%u\n",
        (unsigned long)now, (int)powerState, (int)isPaired, (int)startArmed,
        (long)tof_cm, (long)uwb_distance_cm, batteryPercent,
        (unsigned)belowConsec, (unsigned)aboveConsec);
  }
  
  delay(2);
}

/*
  ================================================================================
  END OF TAG BEACON FIRMWARE (UWB Communication Only)
  ================================================================================
  
  COMMUNICATION ARCHITECTURE:
  - BLE: Pairing and configuration ONLY
  - UWB: ALL timing data (start events, distance, battery, etc.)
  - NO ESP-NOW (completely removed)
  
  PAIRING FLOW:
  1. User opens app → connects to Anchor via BLE
  2. User taps "Add Tag" → app scans for Tags
  3. App reads Tag's UWB address from BLE_UWB_INFO_UUID
  4. App writes Anchor's network config to BLE_NETWORK_CFG_UUID
  5. Tag saves config and configures UWB module
  6. Tag confirms pairing (green flash)
  7. All subsequent data flows via UWB
  
  UWB DATA PACKET (sent every 300ms):
  - Battery percentage (1 byte)
  - Distance to Anchor (2 bytes)
  - Start event age (4 bytes, -1 if no start)
  
  ================================================================================
*/
