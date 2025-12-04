# Tag Beacon (Start Gate) - Technical Documentation

> **Optical start detection with UWB communication for sprint timing**

---

## 📋 Table of Contents

- [Overview](#overview)
- [Hardware Requirements](#hardware-requirements)
- [Wiring Diagram](#wiring-diagram)
- [Firmware Features](#firmware-features)
- [Communication Architecture](#communication-architecture)
- [Power Management](#power-management)
- [LED Status Indicators](#led-status-indicators)
- [Button Controls](#button-controls)
- [Pairing Process](#pairing-process)
- [Start Detection Algorithm](#start-detection-algorithm)
- [Configuration](#configuration)
- [Troubleshooting](#troubleshooting)
- [Development](#development)

---

## 🎯 Overview

The Tag Beacon is the **start gate** component of the Sprint Timer System. It uses a Time-of-Flight (TOF) sensor to optically detect when a runner crosses the start line and communicates this event to the Anchor via Ultra-Wideband (UWB) radio.

### Key Features

✅ **Automatic Start Detection** - No buttons, no beams, just cross the line  
✅ **UWB Communication** - All timing data via UWB (no ESP-NOW, no WiFi)  
✅ **BLE Pairing** - Simple pairing via mobile app  
✅ **Battery Powered** - 8-12 hours active use, months of standby  
✅ **Visual Feedback** - 8x8 LED matrix shows device state  
✅ **OTA Updates** - Firmware updates over WiFi  

### What It Does

1. **Detects Start**: TOF sensor detects runner crossing start line
2. **Sends Start Event**: Transmits start timestamp to Anchor via UWB
3. **Measures Distance**: UWB ranging provides distance to Anchor
4. **Reports Battery**: Sends battery status to Anchor
5. **Visual Feedback**: LED animations show device state

### What It Doesn't Do

❌ **No Finish Detection** - That's the Anchor's job  
❌ **No WiFi During Operation** - Only for OTA updates  
❌ **No ESP-NOW** - All communication via UWB  
❌ **No Standalone Timing** - Must be paired with Anchor  

---

## 🔧 Hardware Requirements

### Required Components

| Component | Specification | Quantity | Approx. Cost |
|-----------|---------------|----------|--------------|
| ESP32-S3 Zero/Mini | Dual-core 240MHz, WiFi, BLE | 1 | $8-12 |
| VL53L1X TOF Sensor | I2C, 4m max range | 1 | $12-15 |
| REYAX RYLR998 | UWB module, 3-10 GHz | 1 | $15-20 |
| WS2812B LED Matrix | 8x8 RGB LEDs (64 total) | 1 | $8-12 |
| 74AHCT125 | Quad level shifter | 1 | $0.50 |
| TP4056 | Li-ion charging board | 1 | $1-2 |
| Li-ion Battery | 3.7V, 1200-2000mAh | 1 | $5-8 |
| Momentary Button | 6mm tactile switch | 1 | $0.10 |
| Resistors | 100KΩ (2x), 10KΩ (2x), 330Ω (1x) | - | $0.50 |
| Capacitor | 1000µF, 6.3V+ electrolytic | 1 | $0.25 |
| USB-C Breakout | For charging | 1 | $2-3 |

**Total Cost (DIY):** ~$60-75 per unit

### Recommended Tools

- Soldering iron (temperature controlled)
- Multimeter
- Wire strippers
- Heat shrink tubing
- Hot glue gun (for strain relief)
- 3D printer (for case) or pre-made enclosure

---

## 🔌 Wiring Diagram

### Pin Assignments

| ESP32-S3 Pin | Connected To | Purpose |
|--------------|--------------|---------|
| GPIO 1 | VL53L1X SDA | I2C data (TOF sensor) |
| GPIO 2 | VL53L1X SCL | I2C clock (TOF sensor) |
| GPIO 44 | UWB RX | UART receive from UWB |
| GPIO 43 | UWB TX | UART transmit to UWB |
| GPIO 6 | LED Matrix DIN | LED data (via level shifter) |
| GPIO 7 | Voltage Divider | Battery voltage sense |
| GPIO 5 | 3.3V Rail Control | Enable 3.3V to peripherals |
| GPIO 4 | Voltage Divider | Charger 5V sense |
| GPIO 10 | TP4056 STDBY | Charge complete detect |
| GPIO 8 | Button | Momentary switch to GND |
| GND | Common Ground | All GND connections |
| 3.3V | ESP32 power | From battery via TP4056 |

### Detailed Connections

#### 1. VL53L1X TOF Sensor

```
VL53L1X          ESP32-S3
--------         --------
VDD     -------> 3.3V (via rail control)
GND     -------> GND
SDA     -------> GPIO 1
SCL     -------> GPIO 2
XSHUT   -------> (not connected)
GPIO1   -------> (not connected)
```

**Important Notes:**
- TOF sensor is powered from **controlled 3.3V rail** (GPIO 5)
- This allows powering off TOF during deep sleep
- Use short wires (<10cm) for I2C to avoid noise

#### 2. REYAX RYLR998 UWB Module

```
RYLR998          ESP32-S3
--------         --------
VCC     -------> 3.3V (via rail control)
GND     -------> GND
TXD     -------> GPIO 44 (ESP32 RX)
RXD     -------> GPIO 43 (ESP32 TX)
RESET   -------> (pull-up to VCC via 10K)
```

**Important Notes:**
- UWB module is powered from **controlled 3.3V rail**
- UART is 3.3V TTL, no level shifting needed
- Add 10KΩ pull-up on RESET to prevent spurious resets

#### 3. WS2812B LED Matrix (8x8)

```
LED Matrix       74AHCT125        ESP32-S3
----------       ---------        --------
VCC     -------> (bypass)  -----> 5V from TP4056
GND     -------> GND
DIN     -------> Output 1A <----- GPIO 6 (Input 1A)
                 VCC       -----> 3.3V
                 GND       -----> GND
```

**Additional Components:**
- **330Ω resistor** in series with GPIO 6 → 74AHCT125 (current limiting)
- **1000µF capacitor** across LED matrix VCC/GND (stabilize power)

**Important Notes:**
- WS2812B requires 5V for bright, consistent colors
- 74AHCT125 level shifts 3.3V → 5V for data line
- Capacitor prevents inrush current from damaging components
- LED matrix draws up to 3.2A at full white (64 LEDs × 50mA)
  - Typical use: ~500-800mA (animations at 200 brightness)

#### 4. Battery Monitoring

```
Battery Voltage Sense (GPIO 7):
                   
Battery+ ----+---- TP4056 OUT+ ----+---- 3.3V Regulator
             |                      |
           100KΩ                  Device
             |                    Power
        +--- GPIO 7
        |
      100KΩ
        |
       GND

Charger Detect (GPIO 4):

USB 5V ------+---- TP4056 IN+
             |
           10KΩ
             |
        +--- GPIO 4
        |
       10KΩ
        |
       GND

TP4056 STDBY -------- GPIO 10
```

**Voltage Divider Calculations:**
- Battery: 4.2V max × (100K / 200K) = 2.1V at ADC (safe for ESP32)
- Charger: 5V × (10K / 20K) = 2.5V at ADC
- Both well within ESP32's 0-3.3V ADC range

#### 5. 3.3V Rail Control

```
                    +--- 3.3V to TOF sensor
                    |
GPIO 5 -----> EN    |--- 3.3V to UWB module
              |     |
           3.3V     +--- (other peripherals)
          Regulator
```

**Purpose:**
- Allows powering off TOF and UWB during deep sleep
- Prevents backfeeding through I2C pullups
- Extends battery life significantly

**Implementation:**
- Use 3.3V LDO regulator with EN pin (e.g., AMS1117-3.3)
- GPIO 5 HIGH = regulator ON (3.3V output)
- GPIO 5 LOW = regulator OFF (0V output)
- Main ESP32 powered from separate always-on 3.3V

#### 6. TP4056 Charging Circuit

```
USB-C Connector
   |
   +---- VBUS (5V) -----> TP4056 IN+
   |
   +---- GND -----------> TP4056 IN-

TP4056 OUT+ -----> Battery+ -----> 3.3V Regulators -----> ESP32
TP4056 OUT- -----> Battery- -----> GND

TP4056 STDBY -----> GPIO 10 (charge complete indicator)
```

**Configuration:**
- Use TP4056 module with protection circuit (TP4056 + DW01A + FS8205A)
- Set charge current via RPROG resistor (1.2KΩ = 1A, 2KΩ = 600mA)
- For 1200mAh battery, use 600mA charge rate (RPROG = 2KΩ)

#### 7. Button

```
                  +3.3V
                    |
                   10KΩ (internal pullup)
                    |
GPIO 8 ----+--------+
           |
         Button
           |
          GND
```

**Function:**
- Short press (<5s): Wake from sleep / Enter sleep
- Long press (≥5s): Enter OTA mode

---

## 🎨 LED Status Indicators

The 8x8 WS2812B LED matrix provides visual feedback for device state.

### Power States

| State | Animation | Color | Meaning |
|-------|-----------|-------|---------|
| **DEEP_SLEEP** | Off | - | Device sleeping (lowest power) |
| **CHARGING** | Rings filling | Blue | Battery charging |
| **FULLY_CHARGED** | Slow pulse | Blue | Battery at 100% |
| **AWAKE_STANDBY** | Single rotating row | Blue | Ready for sprint |
| **AWAKE_RUNNING** | Sine waves | Green | Start triggered, sprint active |
| **PAIRING_MODE** | Medium breathing | Purple | BLE pairing active |
| **OTA_MODE** | Slow breathing | Purple | Firmware update in progress |

### Detailed Animations

#### Standby (Blue Rotating Row)
```
Frame 1: ████████ -------- -------- -------- -------- -------- -------- --------
Frame 2: -------- ████████ -------- -------- -------- -------- -------- --------
Frame 3: -------- -------- ████████ -------- -------- -------- -------- --------
...continues...
```
- **Pattern**: One horizontal row lights up, steps to next row every 250ms
- **Color**: Blue (RGB: 20, 100, 255)
- **Meaning**: Device is awake and ready to detect start

#### Running (Green Waves)
```
Continuous sine wave pattern flowing across matrix
Color: Green (varies 0-255 based on sine calculation)
```
- **Pattern**: Organic flowing green waves
- **Meaning**: Start detected, sprint is active

#### Charging Rings
```
Battery 25%:  Ring 1 (center 2×2) lights up
Battery 50%:  Rings 1-2 light up
Battery 75%:  Rings 1-3 light up  
Battery 100%: All 4 rings light up
```
- **Pattern**: Concentric square rings fill from center
- **Color**: Blue
- **Animation**: Cycles through rings every 2 seconds
- **Meaning**: Shows charging progress

#### Pairing Mode (Purple Breathing)
```
Brightness cycles: 30 → 200 → 30 (over ~3 seconds)
Color: Purple (RGB: brightness, 0, brightness)
```
- **Pattern**: Entire matrix fades in/out
- **Meaning**: Waiting for BLE pairing from app

### Error Indicators

If something goes wrong during initialization, the LED will flash a specific color:

| Color | Meaning | Action |
|-------|---------|--------|
| **Red** | TOF sensor not detected | Check I2C wiring, sensor power |
| **Magenta** | UWB module not responding | Check UART wiring, UWB power |
| **Orange** | Configuration error | Check NVS data, try re-pairing |

---

## 🔘 Button Controls

The momentary button on GPIO 8 provides user control without needing the mobile app.

### Button Press Durations

| Press Duration | Action | Result |
|----------------|--------|--------|
| **Short (<5s)** | Toggle sleep/wake | If asleep → wake up<br>If awake → sleep |
| **Long (≥5s)** | Enter OTA mode | Connect to WiFi, start OTA server |

### Usage Examples

#### Wake from Sleep
1. Device is in deep sleep (LEDs off)
2. Press button briefly (<5 seconds)
3. Release button
4. LEDs turn on with standby animation
5. Device is now awake and ready

#### Enter Sleep Manually
1. Device is awake (LEDs showing standby animation)
2. Press button briefly (<5 seconds)
3. Release button
4. LEDs turn off
5. Device enters deep sleep

#### Enter OTA Mode (Firmware Update)
1. Device is awake or just powered on
2. Press and **hold** button
3. Wait 5+ seconds (count to 5)
4. LEDs turn purple (breathing animation)
5. Release button
6. Device is now in OTA mode, ready for upload

### Sleep Behavior

**Auto-sleep triggers:**
- Charger disconnected while in charging mode
- (Optional) Inactivity timeout (configurable in firmware)

**Auto-wake triggers:**
- USB charger connected
- Button pressed

---

## 🔄 Pairing Process

The Tag must be paired with an Anchor before it can function. Pairing is done via BLE through the mobile app.

### Prerequisites

- ✅ Tag beacon powered on and awake
- ✅ Anchor beacon paired with mobile app
- ✅ Mobile app open and connected to Anchor
- ✅ Bluetooth enabled on phone

### Step-by-Step Pairing

#### 1. Initiate Pairing from App

```
Mobile App
    ↓
[Connect to Anchor] → Select "Anchor-XXXXXX"
    ↓
[Paired Devices] → Tap "Add Tag"
    ↓
App scans for nearby Tags...
```

The app will:
- Send `{"enter_pairing_mode":true}` to Anchor
- Anchor enters pairing mode (if not already paired)
- App scans for BLE devices with "Tag-" prefix

#### 2. Tag Enters Pairing Mode

The Tag automatically enters pairing mode when:
- Not yet paired and powered on for the first time
- Receives pairing command via BLE Control characteristic

**Visual indicator:** Purple breathing animation

#### 3. Select Tag in App

```
App displays discovered Tags:
┌─────────────────────────────────┐
│  Available Tags:                │
│                                 │
│  ○ Tag-A4B2C8  (Battery: 85%)  │
│  ○ Tag-F1E3D9  (Battery: 62%)  │
│  ○ Tag-3C7A91  (Battery: 91%)  │
│                                 │
│  [ Refresh ]          [ Cancel ]│
└─────────────────────────────────┘
```

User taps the desired Tag.

#### 4. App Exchanges UWB Configuration

Behind the scenes:

```kotlin
// 1. App connects to Tag via BLE
val tag = bleManager.connect("Tag-A4B2C8")

// 2. App reads Tag's UWB address
val uwbAddress = tag.readCharacteristic(UUID_UWB_INFO)
// Returns: {"uwb_address":"TAGA4B2C8","device_type":"tag"}

// 3. App writes Anchor's UWB network config to Tag
val anchorConfig = """
{
  "network_id": "REYAX_3F8A92",
  "anchor_address": "ANCH0001"
}
"""
tag.writeCharacteristic(UUID_NETWORK_CFG, anchorConfig)

// 4. Tag saves config to NVS and configures UWB module
```

#### 5. Tag Confirms Pairing

**Visual indicator:** Bright green flash (500ms)

The Tag:
- Saves Anchor's network ID and address to NVS
- Configures UWB module with new network settings
- Sends confirmation back to app via BLE Status characteristic
- Exits pairing mode, returns to standby

#### 6. Pairing Complete

```
Mobile App displays:
┌─────────────────────────────────┐
│  Pairing Successful!            │
│                                 │
│  Tag-A4B2C8 paired to Anchor    │
│  UWB Network: REYAX_3F8A92      │
│  Distance: 15.3m                │
│  Battery: 85%                   │
│                                 │
│  [ Done ]                       │
└─────────────────────────────────┘
```

### UWB Network Configuration

After pairing, the Tag's UWB module is configured with:

| Parameter | Example Value | Purpose |
|-----------|---------------|---------|
| Mode | TAG (0) | Device operates as mobile tag |
| Network ID | REYAX_3F8A92 | Must match Anchor (isolates from other systems) |
| Address | TAGA4B2C8 | Tag's unique UWB address (from MAC) |
| Anchor Address | ANCH0001 | Anchor's UWB address (for directed messages) |

### Unpairing

To unpair a Tag:

1. **Via App:**
   - Connect to Anchor
   - Navigate to Paired Devices
   - Select Tag → "Remove Device"
   - Tag clears stored network configuration

2. **Via Factory Reset:**
   - Hold button during power-on for 10+ seconds
   - All stored data erased (not yet implemented)

### Troubleshooting Pairing

| Issue | Solution |
|-------|----------|
| Tag not appearing in scan | Ensure Tag is in pairing mode (purple breathing) |
| Pairing fails immediately | Check Bluetooth permissions on phone |
| Pairing succeeds but no communication | Verify UWB network ID matches Anchor |
| Tag shows green flash but app says "Failed" | BLE disconnected during write, retry |
| Tag battery dead during pairing | Charge Tag fully before pairing |

---

## 🎯 Start Detection Algorithm

The Tag uses a sophisticated multi-frame algorithm to reliably detect when a runner crosses the start line.

### Overview

The algorithm balances:
- ✅ **Sensitivity**: Detect fast passes through narrow beam
- ✅ **Robustness**: Ignore false triggers (birds, debris, noise)
- ✅ **Latency**: Trigger within 50ms of actual crossing
- ✅ **Re-arm**: Quickly reset for next sprint

### How It Works

#### 1. Baseline Establishment

On power-up or re-arm:
```cpp
// Read TOF sensor
int distance_cm = tof.getDistance() / 10;

// Clamp to maximum baseline
if (distance_cm > 183) distance_cm = 183;  // 6 feet max

// Store as baseline
baseline_cm = distance_cm;
```

**Baseline** = distance to nearest object when no runner present  
Typically: 150-200cm to a wall or post

#### 2. Drop Detection

On each TOF reading (every 20ms):
```cpp
// Calculate drop from baseline
int drop_cm = baseline_cm - current_cm;

// Check for significant drop
if (drop_cm >= 10) {  // MIN_DROP_CM threshold
  // Potential runner detected
}
```

**Why 10cm threshold?**
- Human body width at narrowest: ~20-25cm
- Half-body crossing beam: ~10-12cm drop
- Filters out small objects, hand waves

#### 3. Consecutive Frame Verification

To avoid false triggers from noise:
```cpp
if (drop >= MIN_DROP_CM) {
  belowConsec++;  // Increment counter
  
  if (belowConsec >= 2) {  // BELOW_CONSEC_REQUIRED
    // START TRIGGERED!
    startArmed = false;
    startTimestamp = millis();
  }
}
```

**Requires 2 consecutive frames** below threshold  
= 40ms minimum presence time  
= Filters out momentary noise spikes

#### 4. Trigger Hold Window

After trigger, maintain triggered state for 500ms:
```cpp
if (startTriggered && millis() - lastTriggerMs < 500) {
  // Still in hold window - stay triggered
  // Even if TOF reading goes back above threshold
}
```

**Why hold window?**
- TOF beam is narrow (~27° cone)
- Fast runners may briefly exit beam mid-pass
- Hold prevents missing fast crosses

#### 5. Re-arm Logic

After hold window expires:
```cpp
if (distance > baseline - MIN_DROP_CM) {
  aboveConsec++;
  
  if (aboveConsec >= 2) {  // ABOVE_CONSEC_REQUIRED
    // Runner has passed, re-arm for next sprint
    startArmed = true;
    baseline_cm = current_cm;  // Update baseline
  }
}
```

**Requires 2 consecutive frames** above threshold  
= 40ms minimum clear time  
= Ready for next runner

### Tunable Parameters

These constants can be adjusted in firmware:

| Parameter | Default | Range | Effect |
|-----------|---------|-------|--------|
| `MIN_DROP_CM` | 10 cm | 5-20 cm | Lower = more sensitive, more false triggers |
| `BELOW_CONSEC_REQUIRED` | 2 | 1-5 | Higher = less sensitive, more robust |
| `ABOVE_CONSEC_REQUIRED` | 2 | 1-5 | Higher = slower re-arm |
| `TRIGGER_HOLD_MS` | 500 ms | 100-1000 ms | Longer = catches slower passes |
| `BASELINE_MAX_CM` | 183 cm | 100-400 cm | Maximum baseline distance |

### Example Scenario

**Setup:**
- Tag 3 meters (300cm) from wall
- Baseline: 183cm (clamped to max)
- Runner crosses 1.5m (150cm) from Tag

**Timeline:**

| Time | TOF Reading | Drop | belowConsec | aboveConsec | State |
|------|-------------|------|-------------|-------------|-------|
| t=0 | 183 cm | 0 | 0 | 2 | ARMED |
| t+20ms | 175 cm | 8 cm | 0 | 2 | ARMED (< threshold) |
| t+40ms | 165 cm | 18 cm | 1 | 0 | ARMED (1st below) |
| t+60ms | 160 cm | 23 cm | **2** | 0 | **TRIGGERED!** |
| t+80ms | 170 cm | 13 cm | - | 0 | TRIGGERED (hold) |
| t+100ms | 180 cm | 3 cm | - | 0 | TRIGGERED (hold) |
| ... | ... | ... | - | - | (hold for 500ms) |
| t+560ms | 182 cm | 1 cm | - | 1 | TRIGGERED (1st above) |
| t+580ms | 183 cm | 0 cm | - | **2** | **RE-ARMED** |

**Result:** Start detected at t+60ms with 2-frame verification

### Visualization

```
Distance (cm)
    ↑
200 |     _______________
    |    /               \___
180 |___/                    \___
    |                            
160 |       Runner passes        
    |         here ↓              
140 |                            
    +--------------------------------→ Time
    
    Baseline ────
    Threshold ─ ─ ─
    Reading ▬▬▬▬
    
    State:  ARMED | TRIGGERED | ARMED
```

### Advanced Features (Future)

Possible enhancements:
- **Velocity estimation**: Calculate runner speed from TOF change rate
- **Direction detection**: Determine which way runner is moving
- **Multi-pass detection**: Count multiple passes without re-arming
- **Adaptive threshold**: Auto-adjust based on conditions

---

## ⚙️ Configuration

### Firmware Configuration

Edit these values at the top of `Tag_Beacon.ino`:

#### Debug Settings

```cpp
#define DIAG_VISUAL_ONLY    1    // 0 = Serial debug enabled
                                  // 1 = LED-only (no Serial)

#define DEBUG_MODE          0    // 0 = Normal operation
                                  // 1 = Extra debug output
```

**Recommendations:**
- Set `DIAG_VISUAL_ONLY = 1` for production (saves power)
- Set `DEBUG_MODE = 1` during development/troubleshooting

#### Battery Calibration

```cpp
const float BATT_CAL = 1.08f;    // Voltage calibration factor
```

**How to calibrate:**
1. Measure actual battery voltage with multimeter
2. Compare to value shown in Serial Monitor
3. Calculate: `BATT_CAL = ActualVoltage / ShownVoltage`
4. Update `BATT_CAL` in firmware and re-upload

#### Start Detection Tuning

```cpp
static const uint16_t MIN_DROP_CM = 10;           // Minimum drop to trigger (cm)
static const uint8_t  BELOW_CONSEC_REQUIRED = 2; // Consecutive frames to trigger
static const uint8_t  ABOVE_CONSEC_REQUIRED = 2; // Consecutive frames to re-arm
static const uint32_t TRIGGER_HOLD_MS = 500;     // Hold trigger duration (ms)
```

**Adjust if:**
- **Missing starts**: Decrease `MIN_DROP_CM` or `BELOW_CONSEC_REQUIRED`
- **False triggers**: Increase `MIN_DROP_CM` or `BELOW_CONSEC_REQUIRED`
- **Missing fast passes**: Increase `TRIGGER_HOLD_MS`

### OTA Configuration

Create `secrets.h` file:

```cpp
// secrets.h
const char* WIFI_SSID = "YourWiFiNetwork";
const char* WIFI_PASS = "YourWiFiPassword";
```

**For OTA updates:**
1. Ensure Tag is on same WiFi network as computer
2. Hold button for 5+ seconds to enter OTA mode
3. In Arduino IDE: `Tools → Port → sprint-tag at 192.168.x.x`
4. Upload as normal

### Non-Volatile Storage (NVS)

The Tag stores these values in NVS (survives reboots):

| Key | Type | Purpose |
|-----|------|---------|
| `paired` | bool | Is device paired with Anchor? |
| `uwb_network` | string | Anchor's UWB network ID |
| `anchor_addr` | string | Anchor's UWB address |

**To clear NVS** (factory reset):
```cpp
void clearNVS() {
  prefs.begin("sprint-tag", false);
  prefs.clear();
  prefs.end();
}
```

Add this function and call it once, then remove and re-upload.

---

## 🐛 Troubleshooting

### Common Issues and Solutions

#### Tag won't power on

**Symptoms:** No LEDs, no activity  
**Possible causes:**
- Battery dead
- Battery not connected
- Power switch off (if present)

**Solutions:**
1. Charge battery via USB-C for 30+ minutes
2. Check battery connection (red/black wires secure)
3. Measure battery voltage with multimeter (should be 3.0-4.2V)
4. Try different battery

---

#### LEDs flash red on power-up

**Meaning:** TOF sensor not detected

**Solutions:**
1. Check I2C wiring (GPIO 1 = SDA, GPIO 2 = SCL)
2. Verify TOF has power (3.3V and GND connected)
3. Measure 3.3V rail (should be 3.3V when awake)
4. Check GPIO 5 is HIGH (rail control enabled)
5. Try different TOF sensor

**Advanced debugging:**
```cpp
// Add to setup() after Wire.begin():
Wire.beginTransmission(0x29);  // VL53L1X I2C address
if (Wire.endTransmission() == 0) {
  Serial.println("TOF found at 0x29");
} else {
  Serial.println("TOF not responding");
}
```

---

#### LEDs flash magenta on power-up

**Meaning:** UWB module not responding

**Solutions:**
1. Check UART wiring (GPIO 44 = RX, GPIO 43 = TX)
2. Verify UWB has power (3.3V and GND connected)
3. Check baud rate is 115200
4. Measure voltage on UWB TXD pin (should toggle 0-3.3V)
5. Add 10KΩ pull-up on UWB RESET pin
6. Try different UWB module

**Test UWB manually:**
```cpp
// Add to setup():
UWB.begin(115200, SERIAL_8N1, 44, 43);
UWB.println("AT");
delay(100);
while (UWB.available()) {
  Serial.write(UWB.read());
}
// Should print "+OK" or "+READY"
```

---

#### Tag enters pairing mode but app can't find it

**Solutions:**
1. Check Bluetooth is enabled on phone
2. Grant Location permission to app (required for BLE scan)
3. Move phone closer to Tag (<2 meters)
4. Restart Tag and app
5. Check Tag is advertising:
   ```
   Install "nRF Connect" app on phone
   Scan for BLE devices
   Look for "Tag-XXXXXX"
   ```

---

#### Pairing succeeds but Tag doesn't communicate with Anchor

**Symptoms:** No distance readings, start events not received

**Solutions:**
1. **Check UWB network ID matches:**
   - In Serial Monitor, verify Tag shows correct network
   - Should match Anchor's network ID exactly
   
2. **Verify Tag configured correctly:**
   ```cpp
   // Tag should show in Serial:
   [UWB] Network: REYAX_3F8A92
   [UWB] Address: TAGA4B2C8
   ```

3. **Test UWB communication:**
   - Place Tag and Anchor close together (<5m)
   - Tag Serial Monitor should show distance readings
   - If not, UWB configuration is wrong

4. **Re-pair device:**
   - Unpair from app
   - Power cycle both Tag and Anchor
   - Pair again from scratch

---

#### Start detection not working

**Symptoms:** Tag never triggers, or triggers constantly

**If never triggers:**
1. Check TOF sensor is working:
   ```
   Serial Monitor should show:
   [DBG] tof=150 (or some value)
   ```
2. Verify baseline is set:
   ```
   [TOF] Baseline: 183 cm
   ```
3. Try adjusting sensitivity:
   ```cpp
   MIN_DROP_CM = 5;  // More sensitive
   ```
4. Check TOF is pointed at runner path (not floor/ceiling)

**If triggers constantly:**
1. Increase `MIN_DROP_CM` threshold
2. Check for moving objects in sensor path (flags, banners, etc.)
3. Verify baseline is reasonable (50-200cm)
4. Check for electrical noise:
   - Add 0.1µF capacitor near TOF VDD/GND
   - Move away from motors, fluorescent lights

---

#### Battery drains quickly

**Expected battery life:**
- Active use: 8-12 hours
- Standby (awake): 24-48 hours
- Deep sleep: 3-6 months

**If draining faster:**
1. Check for constantly lit LEDs (power state stuck)
2. Verify deep sleep is working:
   ```
   Press button <5s → LEDs should turn off
   ```
3. Measure current draw:
   - Deep sleep: <1mA
   - Standby: 50-80mA
   - Active (LEDs on): 200-500mA
4. Check for battery drain when off (should be 0mA)
5. Try different battery (may be degraded)

---

#### Tag won't enter deep sleep

**Symptoms:** Button press <5s doesn't turn off LEDs

**Solutions:**
1. Check button wiring (GPIO 8 to GND when pressed)
2. Verify button press is detected:
   ```
   Serial Monitor should show:
   [BUTTON] <5s -> sleep
   ```
3. Check for stuck power state
4. Try holding button during power-up to reset

---

#### OTA not working

**Symptoms:** Can't find device in Arduino IDE, upload fails

**Solutions:**
1. Verify Tag is in OTA mode (purple breathing LEDs)
2. Check WiFi connection:
   ```
   Serial Monitor should show:
   [OTA] Connected to WiFi
   [OTA] IP: 192.168.1.100
   ```
3. Computer and Tag must be on same network
4. Try pinging Tag's IP address
5. Check firewall isn't blocking OTA port (3232)
6. Use USB upload instead:
   - Connect USB-C cable
   - Select COM port in Arduino IDE
   - Upload normally

---

#### Distance readings incorrect

**Symptoms:** App shows wrong distance to Anchor

**Solutions:**
1. Check UWB antennas are vertical (perpendicular to ground)
2. Move away from metal objects, walls
3. Verify both Tag and Anchor are paired correctly
4. Test in open space first (no obstacles)
5. Check Serial Monitor for distance readings:
   ```
   [UWB] Distance = 1543 cm (should be ~15m if 15m apart)
   ```
6. Calibrate if necessary (not currently implemented)

---

### Getting Help

If you can't resolve the issue:

1. **Check Serial Monitor output:**
   - Set `DIAG_VISUAL_ONLY = 0`
   - Upload firmware
   - Open Serial Monitor at 115200 baud
   - Copy all output

2. **Note LED behavior:**
   - What color/pattern?
   - When does it occur?
   - Does it change?

3. **Check wiring:**
   - Take clear photos
   - Compare to wiring diagram
   - Measure voltages with multimeter

4. **Open GitHub issue:**
   - Include Serial Monitor output
   - Include LED behavior description
   - Include photos of hardware
   - State what you've already tried

---

## 💻 Development

### Building from Source

#### Prerequisites

1. **Arduino IDE 2.0+** or **PlatformIO**
2. **ESP32 board support** (v2.0.11+)
3. **Required libraries:**
   ```
   - FastLED (3.6.0+)
   - NimBLE-Arduino (1.4.1+)
   - SparkFun VL53L1X (1.3.0+)
   ```

#### Setup

```bash
# Clone repository
git clone https://github.com/yourusername/sprint-timer.git
cd sprint-timer/firmware/tag

# Create secrets.h
cp secrets.h.template secrets.h
# Edit secrets.h with your WiFi credentials

# Open in Arduino IDE
arduino Tag_Beacon.ino
```

#### Configure Board

```
Tools → Board → ESP32 Arduino → ESP32S3 Dev Module
Tools → USB CDC On Boot → Enabled
Tools → Flash Size → 4MB (with 1.2MB APP/1.5MB SPIFFS)
Tools → Partition Scheme → Default 4MB with spiffs
Tools → Upload Speed → 921600
Tools → Port → [Select your COM port]
```

#### Upload

1. Connect ESP32-S3 via USB-C
2. Click Upload button
3. Wait for "Done uploading"

### Testing

#### Unit Tests

Test individual components:

```cpp
// Test TOF sensor
void testTOF() {
  Wire.begin(1, 2);
  if (tof.begin() == 0) {
    tof.startRanging();
    delay(100);
    if (tof.checkForDataReady()) {
      int dist = tof.getDistance();
      Serial.printf("TOF distance: %d mm\n", dist);
    }
  }
}

// Test UWB module
void testUWB() {
  UWB.begin(115200, SERIAL_8N1, 44, 43);
  UWB.println("AT");
  delay(100);
  while (UWB.available()) {
    Serial.write(UWB.read());
  }
}

// Test LED matrix
void testLEDs() {
  FastLED.addLeds<WS2812B, 6, GRB>(leds, 64);
  fill_solid(leds, 64, CRGB::Red);
  FastLED.show();
  delay(1000);
  fill_solid(leds, 64, CRGB::Green);
  FastLED.show();
  delay(1000);
  fill_solid(leds, 64, CRGB::Blue);
  FastLED.show();
}
```

#### Integration Testing

1. **Pairing Test:**
   - Power on Tag (unpaired)
   - Open app → Connect to Anchor
   - Add Tag via app
   - Verify green flash on Tag
   - Check Serial: `[BLE] Pairing complete!`

2. **Start Detection Test:**
   - Place Tag at start line
   - Wave hand across TOF sensor
   - Verify green waves animation
   - Check Serial: `[START] TRIGGERED!`

3. **UWB Communication Test:**
   - Pair Tag with Anchor
   - Place devices 5-10m apart
   - Check Serial: `[UWB] Distance = XXX cm`
   - Move closer/farther, verify distance changes

4. **Battery Test:**
   - Check battery % in app
   - Compare to multimeter reading
   - Discharge battery, verify % decreases
   - Charge battery, verify charging animation

### Code Style

Follow these conventions:

```cpp
// Constants: UPPER_CASE
#define PIN_LED 6
static const uint16_t MIN_DROP_CM = 10;

// Variables: camelCase
int currentDistance = 0;
bool isArmed = true;

// Functions: camelCase
void updateLEDs() { }
int readBattery() { }

// Classes: PascalCase
class SensorManager { };

// Comments: Sentence case with period
// This function reads the TOF sensor and returns distance in cm.
```

### Adding Features

To add a new feature:

1. **Plan the feature:**
   - What does it do?
   - What are inputs/outputs?
   - How does it interact with existing code?

2. **Write the code:**
   - Add functions in logical order
   - Comment thoroughly
   - Use existing patterns

3. **Test thoroughly:**
   - Test normal operation
   - Test edge cases
   - Test failure modes

4. **Document:**
   - Update this README
   - Add comments to code
   - Update user manual

5. **Submit PR:**
   - Create feature branch
   - Commit with clear messages
   - Open pull request with description

---

## 📚 Additional Resources

- **[Project Main README](../../README.md)** - Full system overview
- **[Anchor Documentation](../anchor/README.md)** - Finish gate details
- **[Mobile App Documentation](../../mobile-app/README.md)** - App usage guide
- **[Hardware Schematics](../../hardware/schematics/)** - PCB designs
- **[3D Models](../../hardware/3d-models/)** - Printable cases

---

## 📄 License

This project is licensed under the MIT License. See [LICENSE](../../LICENSE) for details.

---

**Built for speed. ⚡**
