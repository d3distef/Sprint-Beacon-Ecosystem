# Sprint Timer System

> **Professional sprint timing system with automatic start/finish detection, wireless distance measurement, and comprehensive mobile app.**

---

## 📋 Table of Contents

- [Overview](#overview)
- [System Architecture](#system-architecture)
- [Hardware Components](#hardware-components)
- [Communication Protocols](#communication-protocols)
- [Getting Started](#getting-started)
- [Product Bundles](#product-bundles)
- [Features](#features)
- [Technical Specifications](#technical-specifications)
- [Development Setup](#development-setup)
- [Repository Structure](#repository-structure)
- [Contributing](#contributing)
- [License](#license)

---

## 🎯 Overview

The Sprint Timer System is a modular, professional-grade timing solution for measuring sprint performance. It combines optical sensors, ultra-wideband (UWB) ranging, and IMU data to provide accurate, automatic timing with no manual intervention required.

### Key Benefits

✅ **Automatic Start Detection** - No buttons to press, timing starts when you cross the line  
✅ **Wireless Distance Measurement** - UWB provides accurate distance without tape measures  
✅ **Battery Powered** - Rechargeable Li-ion batteries, no wires needed  
✅ **Mobile App** - View results, manage devices, track history  
✅ **Modular System** - Buy what you need, expand later  
✅ **Multi-Runner Support** - Time multiple athletes simultaneously with wearables  

---

## 🏗️ System Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                      Mobile App (Android)                    │
│  • BLE connection to all devices (pairing & config)         │
│  • Displays timing results                                   │
│  • Device management & settings                              │
│  • Historical data & analytics                               │
└─────────────────────────────────────────────────────────────┘
                          │ BLE (pairing only)
                          ↓
┌─────────────────────────────────────────────────────────────┐
│                    ANCHOR (Finish Gate)                      │
│  • ESP32-S3 Zero + VL53L1X TOF + REYAX UWB                  │
│  • Detects finish line crossing                              │
│  • Receives start events via UWB                             │
│  • Calculates sprint time                                    │
│  • 8x8 LED matrix for visual feedback                        │
└─────────────────────────────────────────────────────────────┘
                          ↑ UWB (all timing data)
                          │
    ┌─────────────────────┴─────────────────────┐
    │                                             │
┌───┴────────────────────┐          ┌───────────┴──────────────┐
│   TAG (Start Gate)     │          │  WEARABLE (Optional)     │
│ • Auto-start detection │          │ • IMU for acceleration   │
│ • UWB distance to      │          │ • UWB positioning        │
│   Anchor               │          │ • Per-runner data        │
│ • BLE pairing          │          │ • BLE pairing            │
└────────────────────────┘          └──────────────────────────┘
```

### Communication Flow

1. **Pairing Phase** (BLE):
   - Mobile app connects to Anchor via BLE
   - App scans for Tags/Wearables
   - App reads each device's UWB address
   - App configures all devices with Anchor's UWB network
   - Devices save config to non-volatile storage

2. **Runtime Phase** (UWB):
   - Tag detects start → sends start timestamp via UWB
   - Anchor detects finish → calculates sprint time
   - Wearable sends IMU data via UWB (if present)
   - All data displayed in mobile app via BLE

---

## 🔧 Hardware Components

### Required: Anchor Beacon (Finish Gate)

The Anchor is the hub of the system and the only required component.

**Hardware:**
- ESP32-S3 Zero/Mini
- VL53L1X Time-of-Flight (TOF) sensor
- REYAX RYLR998 UWB module
- WS2812B 8x8 LED matrix (64 LEDs)
- TP4056 charging board
- 3.7V Li-ion battery (1200-2000mAh recommended)
- 74AHCT125 level shifter for LEDs
- Momentary push button

**Functions:**
- Finish line detection (TOF sensor)
- BLE hub for pairing
- UWB receiver for start events
- Distance calculation
- Sprint time calculation
- Battery monitoring
- LED status display

### Optional: Tag Beacon (Start Gate)

Add automatic start detection and distance measurement.

**Hardware:**
- ESP32-S3 Zero/Mini
- VL53L1X TOF sensor
- REYAX RYLR998 UWB module
- WS2812B 8x8 LED matrix
- TP4056 charging board
- 3.7V Li-ion battery
- 74AHCT125 level shifter
- Momentary push button

**Functions:**
- Start line detection (TOF sensor)
- UWB transmitter (sends start events)
- Distance measurement to Anchor
- BLE pairing with Anchor
- Battery monitoring
- LED status display

**Use Cases:**
- **1 Tag**: Auto-start timing
- **2+ Tags**: Split timing at multiple points
- **No Tag**: Manual start via app (Anchor only)

### Optional: Wearable Module

Add per-runner IMU data and multi-runner support.

**Hardware:**
- ESP32-S3 Zero/Mini
- ICM-20948 9-axis IMU (accelerometer, gyroscope, magnetometer)
- REYAX RYLR998 UWB module
- Small OLED display (optional)
- TP4056 charging board
- 3.7V Li-ion battery (smaller form factor)
- Momentary push button

**Functions:**
- Acceleration measurement
- UWB positioning
- Per-runner data tagging
- BLE pairing with Anchor
- Battery monitoring

**Use Cases:**
- Multiple runners timing simultaneously
- Acceleration profiling
- Form analysis
- Competitive training

---

## 📡 Communication Protocols

### Bluetooth Low Energy (BLE)

**Purpose:** Device pairing and configuration ONLY

**Characteristics:**

| UUID Suffix | Name | Type | Purpose |
|-------------|------|------|---------|
| ...e0b2 | Status | READ\|NOTIFY | Device status (battery, state, etc.) |
| ...e0c6 | UWB Info | READ | Device's UWB address |
| ...e0c7 | Network Config | WRITE | Anchor's UWB network settings |
| ...e0c5 | Control | WRITE | Commands (pairing mode, etc.) |

**When Used:**
- Initial device pairing
- Device configuration
- Battery level monitoring (when connected)
- Firmware updates

### Ultra-Wideband (UWB)

**Purpose:** ALL timing data and runtime communication

**Module:** REYAX RYLR998  
**Frequency:** 3-10 GHz  
**Range:** Up to 1000m line-of-sight  
**Accuracy:** ±10cm  

**Data Packets:**

1. **Tag → Anchor** (every 300ms):
   ```
   Battery % (1 byte)
   Distance cm (2 bytes)
   Start age ms (4 bytes, -1 if no start)
   ```

2. **Wearable → Anchor** (every 100ms):
   ```
   Device ID (2 bytes)
   Battery % (1 byte)
   Accel X/Y/Z (6 bytes)
   Distance cm (2 bytes)
   ```

3. **Anchor → Tag/Wearable** (on request):
   ```
   Distance cm (2 bytes)
   RSSI (1 byte)
   ```

**Why UWB?**
- ✅ Accurate distance measurement (±10cm vs ±5m for Bluetooth)
- ✅ Better penetration through obstacles
- ✅ Lower latency than BLE for timing data
- ✅ No pairing required after initial config
- ✅ Multiple devices simultaneously

---

## 🚀 Getting Started

### Quick Start Guide

#### 1. Assemble Your Hardware

**Minimum Setup (Anchor only):**
1. Flash Anchor firmware to ESP32-S3
2. Assemble Anchor hardware (see wiring diagrams)
3. Charge battery
4. Power on

**Recommended Setup (Anchor + Tag):**
1. Flash Anchor firmware to first ESP32-S3
2. Flash Tag firmware to second ESP32-S3
3. Assemble both devices
4. Charge both batteries
5. Power on both devices

#### 2. Install Mobile App

```bash
# Clone repository
git clone https://github.com/yourusername/sprint-timer.git

# Open Android project
cd sprint-timer/mobile-app
# Open in Android Studio
```

Build and install APK to your Android phone.

#### 3. Pair Devices

1. **Open App** → Tap "Add Device"
2. **Connect to Anchor** → Select "Anchor-XXXXXX" from BLE scan
3. **Add Tag** (if you have one):
   - In app, tap "Paired Devices" → "Add Tag"
   - Select "Tag-XXXXXX" from scan list
   - Wait for green flash on Tag (pairing complete)
4. **Test Connection** → App shows "Connected" with battery levels

#### 4. Run Your First Sprint

**With Tag (auto-start):**
1. Place Anchor at finish line
2. Place Tag at start line (20-40 yards away)
3. Wait for blue rotating animation (both devices ready)
4. Cross Tag's TOF sensor → See fire animation (start detected)
5. Cross Anchor's TOF sensor → See rainbow animation (finish!)
6. View time in app

**Without Tag (manual start):**
1. Place Anchor at finish line
2. Open app → Tap "Arm"
3. Wait for random countdown (2-5 seconds)
4. Sprint starts automatically → timer begins
5. Cross Anchor's TOF sensor → timer stops
6. View time in app

---

## 📦 Product Bundles

### Bundle 1: Basic (Anchor Only) - $149

**What's Included:**
- 1x Anchor beacon (fully assembled)
- 1x USB-C charging cable
- 1x Carrying case
- Mobile app (free download)

**What You Can Do:**
- Manual start timing (app initiates countdown)
- Automatic finish detection
- Store unlimited sprint data
- Track history and progress

**Best For:**
- Individual athletes
- Basic sprint timing
- Budget-conscious buyers

---

### Bundle 2: Pro (Anchor + Tag) - $249

**What's Included:**
- 1x Anchor beacon
- 1x Tag beacon
- 2x USB-C charging cables
- 1x Carrying case
- Mobile app (free download)

**What You Can Do:**
- **Automatic start detection** (no buttons!)
- **Automatic distance measurement** (UWB ranging)
- Automatic finish detection
- Split timing capability
- Store unlimited sprint data
- Track history with distance context

**Best For:**
- Serious athletes
- Coaches training individuals
- Anyone wanting automatic timing

---

### Bundle 3: Team (Anchor + 2 Tags + 3 Wearables) - $549

**What's Included:**
- 1x Anchor beacon
- 2x Tag beacons (start + 10yd split)
- 3x Wearable modules
- 6x USB-C charging cables
- 1x Carrying case
- Mobile app (free download)

**What You Can Do:**
- **Multi-runner simultaneous timing**
- **Split timing** (0-10yd, 10-40yd, etc.)
- **Acceleration profiling** (wearable IMU data)
- Automatic distance measurement
- Per-runner data tracking
- Team analytics

**Best For:**
- Teams and group training
- Coaches with multiple athletes
- Competitive training programs
- Sports performance facilities

---

## ✨ Features

### Timing Features

- **Automatic Start Detection**: TOF sensor triggers timer when runner crosses start line
- **Automatic Finish Detection**: TOF sensor at finish line stops timer
- **Split Timing**: Add multiple Tags at different distances for split times
- **Manual Start Mode**: App-initiated countdown (2-5 seconds random) for reaction time training
- **Millisecond Precision**: Accurate to ±1ms

### Measurement Features

- **Wireless Distance**: UWB provides distance measurement (±10cm accuracy)
- **No Tape Measure**: Distance automatically calculated and stored with each sprint
- **Range Locking**: Distance locked when sprint starts (prevents interference)
- **Distance Display**: Real-time distance shown in app

### Mobile App Features

- **Live Timing Display**: Large, auto-sizing timer during sprint
- **Results Dashboard**: Sprint time, average MPH, distance
- **History Tracking**: Unlimited sprint storage with timestamps
- **Multi-User Support**: Add multiple runners, track individually
- **Data Export**: Export to CSV for analysis
- **Analytics Charts**: 
  - Average MPH over time
  - Speed distribution histograms
  - Leaderboards (fastest sprints)
  - Consistency metrics (std deviation)
- **Filtering**: Filter by runner, date range, distance

### Hardware Features

- **Battery Monitoring**: Real-time battery % for all devices
- **Charging Status**: Visual indicators when charging
- **Deep Sleep**: Automatic sleep when not in use (months of standby)
- **LED Feedback**: 8x8 matrix shows device state
- **OTA Updates**: Firmware updates over WiFi (no USB needed)
- **Waterproof Cases**: IP67 rated enclosures (optional)

### Power Management

- **Smart Sleep**: Automatically enters deep sleep when not in use
- **Wake on Charger**: Plugging in charger wakes device
- **Wake on Button**: Short press wakes from sleep
- **Charging Animation**: Shows battery level while charging
- **Low Battery Warning**: LED indicates low battery
- **Auto Power-Off**: Configurable timeout

---

## 📊 Technical Specifications

### Timing Performance

| Metric | Specification |
|--------|---------------|
| Timing Accuracy | ±1 millisecond |
| Start Detection Latency | <50ms |
| Finish Detection Latency | <50ms |
| Maximum Sprint Distance | 1000m (UWB range) |
| Distance Accuracy | ±10cm |

### Hardware Specifications

#### Anchor Beacon

| Component | Specification |
|-----------|---------------|
| Microcontroller | ESP32-S3 (240 MHz dual-core) |
| TOF Sensor | VL53L1X (4m max range) |
| UWB Module | REYAX RYLR998 (3-10 GHz) |
| LED Display | WS2812B 8x8 (64 LEDs) |
| Battery | 3.7V Li-ion, 1200-2000mAh |
| Charging | USB-C, 5V 1A |
| Battery Life | 8-12 hours active use |
| Standby Time | 3-6 months |
| Dimensions | 100mm x 100mm x 40mm |
| Weight | 150g with battery |

#### Tag Beacon

| Component | Specification |
|-----------|---------------|
| Microcontroller | ESP32-S3 (240 MHz dual-core) |
| TOF Sensor | VL53L1X (4m max range) |
| UWB Module | REYAX RYLR998 (3-10 GHz) |
| LED Display | WS2812B 8x8 (64 LEDs) |
| Battery | 3.7V Li-ion, 1200-2000mAh |
| Charging | USB-C, 5V 1A |
| Battery Life | 8-12 hours active use |
| Standby Time | 3-6 months |
| Dimensions | 100mm x 100mm x 40mm |
| Weight | 150g with battery |

#### Wearable Module

| Component | Specification |
|-----------|---------------|
| Microcontroller | ESP32-S3 (240 MHz dual-core) |
| IMU | ICM-20948 (9-axis) |
| UWB Module | REYAX RYLR998 (3-10 GHz) |
| Display | 0.96" OLED (optional) |
| Battery | 3.7V Li-ion, 500-1000mAh |
| Charging | USB-C, 5V 1A |
| Battery Life | 6-8 hours active use |
| Standby Time | 1-2 months |
| Dimensions | 60mm x 40mm x 20mm |
| Weight | 50g with battery |

### Environmental Specifications

| Parameter | Rating |
|-----------|--------|
| Operating Temperature | -10°C to 50°C (14°F to 122°F) |
| Storage Temperature | -20°C to 60°C (-4°F to 140°F) |
| Humidity | 10-90% non-condensing |
| Water Resistance | IP67 with optional case |
| Drop Resistance | 1.2m onto concrete |

---

## 💻 Development Setup

### Prerequisites

**Hardware:**
- ESP32-S3 Zero or Mini development boards
- VL53L1X TOF sensors
- REYAX RYLR998 UWB modules
- WS2812B LED strips or matrices
- TP4056 charging boards
- 3.7V Li-ion batteries
- USB-C cables
- Breadboards and jumper wires (for prototyping)

**Software:**
- Arduino IDE 2.x or PlatformIO
- Android Studio (for mobile app)
- Git

### Firmware Setup

#### 1. Install Arduino IDE

Download from: https://www.arduino.cc/en/software

#### 2. Install ESP32 Board Support

```
Arduino IDE → Preferences → Additional Board Manager URLs:
https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json
```

Then: `Tools → Board → Boards Manager → Search "esp32" → Install`

#### 3. Install Required Libraries

```
Tools → Manage Libraries → Install:
- FastLED
- NimBLE-Arduino
- SparkFun VL53L1X
- Preferences (built-in)
```

#### 4. Create secrets.h

Create `secrets.h` in same folder as sketch:

```cpp
// secrets.h
const char* WIFI_SSID = "YourWiFiName";
const char* WIFI_PASS = "YourWiFiPassword";
```

#### 5. Configure Board

```
Tools → Board → esp32 → ESP32S3 Dev Module
Tools → Flash Size → 4MB
Tools → Partition Scheme → Default 4MB with spiffs
Tools → Upload Speed → 921600
```

#### 6. Upload Firmware

1. Connect ESP32-S3 via USB-C
2. Select correct COM port: `Tools → Port`
3. Click Upload button
4. Wait for "Done uploading" message

### Mobile App Setup

#### 1. Install Android Studio

Download from: https://developer.android.com/studio

#### 2. Clone Repository

```bash
git clone https://github.com/yourusername/sprint-timer.git
cd sprint-timer/mobile-app
```

#### 3. Open Project

```
Android Studio → Open → Select mobile-app folder
Wait for Gradle sync to complete
```

#### 4. Connect Android Device

1. Enable Developer Options on phone
2. Enable USB Debugging
3. Connect via USB
4. Allow USB debugging when prompted

#### 5. Build and Run

```
Run → Run 'app'
Select your connected device
Wait for build and installation
```

---

## 📁 Repository Structure

```
sprint-timer/
├── firmware/
│   ├── anchor/
│   │   ├── Anchor_Beacon.ino          # Main Anchor firmware
│   │   ├── secrets.h.template         # WiFi credentials template
│   │   └── README.md                  # Anchor-specific documentation
│   ├── tag/
│   │   ├── Tag_Beacon.ino             # Main Tag firmware  
│   │   ├── secrets.h.template         # WiFi credentials template
│   │   └── README.md                  # Tag-specific documentation
│   ├── wearable/
│   │   ├── Wearable_Module.ino        # Main Wearable firmware
│   │   ├── secrets.h.template         # WiFi credentials template
│   │   └── README.md                  # Wearable-specific documentation
│   └── common/
│       ├── battery_monitor.ino        # Shared battery code
│       └── led_animations.ino         # Shared LED functions
├── mobile-app/
│   ├── app/
│   │   ├── src/
│   │   │   ├── main/
│   │   │   │   ├── java/
│   │   │   │   │   └── com/example/mvpble/
│   │   │   │   │       ├── MainActivity.kt      # Main screen
│   │   │   │   │       ├── HistoryScreen.kt     # History/analytics
│   │   │   │   │       └── BleManager.kt        # BLE handling
│   │   │   │   ├── res/                         # Resources
│   │   │   │   └── AndroidManifest.xml
│   │   │   └── build.gradle
│   │   └── README.md
│   └── build.gradle
├── hardware/
│   ├── schematics/
│   │   ├── anchor_schematic.pdf
│   │   ├── tag_schematic.pdf
│   │   └── wearable_schematic.pdf
│   ├── pcb/
│   │   ├── anchor_pcb_gerbers.zip
│   │   ├── tag_pcb_gerbers.zip
│   │   └── wearable_pcb_gerbers.zip
│   ├── 3d-models/
│   │   ├── anchor_case.stl
│   │   ├── tag_case.stl
│   │   └── wearable_case.stl
│   └── bom/
│       ├── anchor_bom.csv              # Bill of materials
│       ├── tag_bom.csv
│       └── wearable_bom.csv
├── docs/
│   ├── user-manual.pdf                # End-user documentation
│   ├── assembly-guide.pdf             # Hardware assembly
│   ├── pairing-guide.pdf              # Device pairing instructions
│   ├── troubleshooting.pdf            # Common issues and fixes
│   └── api-reference.md               # BLE/UWB protocol docs
├── tests/
│   ├── firmware-tests/
│   │   ├── test_tof_sensor.ino
│   │   ├── test_uwb_module.ino
│   │   └── test_ble_pairing.ino
│   └── app-tests/
│       └── BleManagerTest.kt
├── LICENSE
├── README.md                          # This file
└── CONTRIBUTING.md
```

---

## 🤝 Contributing

We welcome contributions! Please see [CONTRIBUTING.md](CONTRIBUTING.md) for details.

### Ways to Contribute

- 🐛 **Report Bugs**: Open an issue with details
- 💡 **Suggest Features**: Open an issue with "[Feature Request]" prefix
- 📝 **Improve Documentation**: Submit PRs for docs
- 🔧 **Fix Issues**: Check "good first issue" label
- 🧪 **Add Tests**: Help improve test coverage

### Development Workflow

1. Fork the repository
2. Create a feature branch (`git checkout -b feature/amazing-feature`)
3. Commit your changes (`git commit -m 'Add amazing feature'`)
4. Push to the branch (`git push origin feature/amazing-feature`)
5. Open a Pull Request

---

## 📄 License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.

---

## 🙏 Acknowledgments

- **SparkFun** - VL53L1X Arduino library
- **REYAX** - UWB module and documentation
- **FastLED** - LED control library
- **NimBLE** - Efficient BLE stack for ESP32
- **Community Contributors** - Thank you!

---

## 📞 Support

- **Documentation**: https://sprint-timer.readthedocs.io
- **Issues**: https://github.com/yourusername/sprint-timer/issues
- **Discussions**: https://github.com/yourusername/sprint-timer/discussions
- **Email**: support@sprint-timer.com

---

## 🗺️ Roadmap

### v2.0 (Current)
- ✅ BLE pairing system
- ✅ UWB-only communication
- ✅ Multi-user support
- ✅ History and analytics

### v2.1 (Q2 2025)
- [ ] Wearable module firmware
- [ ] Multi-runner simultaneous timing
- [ ] IMU data visualization
- [ ] Cloud sync (optional)

### v2.2 (Q3 2025)
- [ ] iOS app
- [ ] Web dashboard
- [ ] Team management features
- [ ] Coach analytics tools

### v3.0 (Q4 2025)
- [ ] AI-powered form analysis
- [ ] Video sync capability
- [ ] Advanced biomechanics
- [ ] Competition mode

---

**Built with ❤️ for athletes, by athletes**
