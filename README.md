<div align="center">

<img src="https://img.shields.io/badge/STM32F411CEU6-Black%20Pill-03234B?style=for-the-badge&logo=stmicroelectronics&logoColor=white"/>
<img src="https://img.shields.io/badge/LoRa-433%20MHz-00C896?style=for-the-badge"/>
<img src="https://img.shields.io/badge/Firebase-RTDB-FF6F00?style=for-the-badge&logo=firebase&logoColor=white"/>
<img src="https://img.shields.io/badge/Random%20Forest-99.2%25-EF4444?style=for-the-badge"/>
<img src="https://img.shields.io/badge/Flask-Dashboard-000000?style=for-the-badge&logo=flask&logoColor=white"/>

# ⚡ Intelligent Street Lighting System
### AI-Enhanced IoT Platform for Predictive Fault Detection & Automated Control

*STM32F411CEU6 · LoRa Ra-02 · ESP32 · Firebase · Random Forest · Flask*

---

</div>

## 🔍 Problem Statement

Traditional street lighting maintenance is **reactive** — a technician is dispatched only after a light fails or a complaint is received. This leads to:

- Delayed fault response causing safety hazards
- No visibility into *where* on the line a fault occurred
- Wasted energy from lights running without need
- Linemen unable to locate broken segments during storms

**ISS solves this** by placing intelligent sensor nodes on every streetlight pole, continuously monitoring electrical parameters and transmitting data wirelessly to the cloud — where an AI model classifies faults in real time and a dashboard shows the exact segment that broke.

---

## 🏗️ System Architecture


---

## ⚙️ Hardware Specification

### Microcontroller — STM32F411CEU6 Black Pill

| Parameter | Value |
|-----------|-------|
| Core | ARM Cortex-M4F with FPU |
| Clock | HSE 25 MHz → PLL → **96 MHz** |
| Flash / RAM | 512 KB / 128 KB |
| Toolchain | STM32CubeIDE + HAL |

### Pin Map

| Peripheral | Pins | Notes |
|------------|------|-------|
| UART2 Debug | PA2/PA3 | 115200 baud |
| ADC1 Voltage | PA4 (CH4) | ZMPT101B, cal = 218.0 |
| ADC1 Current | PA5 (CH5) | SCT013, cal = 85.7, 33Ω burden |
| I2C1 | PB6/PB7 | BH1750 @ 0x23 + LCD @ 0x27 |
| SPI1 | PB3/PA6/PA7 | LoRa Ra-02 SX1278 |
| LORA_NSS | PA8 | Active LOW chip-select |
| LORA_RST | PA9 | Active LOW reset |
| LORA_DIO0 | PB4 | TxDone — polled via SPI |
| Relay | PB0 | Active LOW |
| PIR | PB1 | Active HIGH + pull-down |
| LED | PC13 | Active LOW (onboard) |

---

## 📡 LoRa Configuration

| Parameter | Value |
|-----------|-------|
| Module | Ra-02 — SX1278 |
| Frequency | 433 MHz |
| Spreading Factor | SF7 |
| Bandwidth | 125 kHz |
| Coding Rate | CR 4/5 |
| Sync Word | 0x12 |
| TX Power | 2 dBm (bench) → 17 dBm (deployment) |
| Payload | 20 bytes — `ISS_Payload_t` packed binary |

---

## 📦 ISS Payload Structure

20-byte packed binary struct — `#pragma pack(1)` ensures no padding on both ARM Cortex-M4 and Xtensa LX6.

```c
#pragma pack(push, 1)
typedef struct {
    float   voltage_rms;   // AC mains voltage RMS       (Volts)
    float   current_rms;   // AC load current RMS         (Amps)
    float   power_w;       // Apparent power = V × I      (Watts)
    float   lux;           // Ambient light — BH1750       (Lux)
    uint8_t pir_state;     // 0 = no motion, 1 = motion
    uint8_t relay_state;   // 0 = OFF, 1 = ON
    uint8_t night_mode;    // 0 = Day (lux ≥ 50), 1 = Night
    uint8_t node_id;       // Node identifier — 0x01
} ISS_Payload_t;           // Total: exactly 20 bytes
#pragma pack(pop)

_Static_assert(sizeof(ISS_Payload_t) == 20, "Size mismatch!");
```

---

## 🧠 AI Fault Detection Model

### Pipeline


### Results

| Fault Class | Precision | Recall | F1 |
|-------------|-----------|--------|----|
| Normal | 0.991 | 0.990 | 0.991 |
| Overvoltage | 0.990 | 0.980 | 0.985 |
| Undervoltage | 0.980 | 0.990 | 0.985 |
| Short Circuit | 0.990 | 1.000 | 0.995 |
| Power Outage | 1.000 | 1.000 | 1.000 |
| **Overall** | **—** | **—** | **99.2%** |

### Feature Importance

| Rank | Feature | Importance |
|------|---------|------------|
| 1 | voltage_rms | 48.2% |
| 2 | current_rms | 31.0% |
| 3 | power_w | 18.4% |
| 4 | lux | 2.3% |

> Model trained on synthetic data matched to ZMPT101B and SCT013 calibration parameters. Will be retrained on real field data.

---

## 🌐 Dashboard Features

- **Fault banner** — color-coded (green / yellow / red) with AI label and confidence %
- **Sensor cards** — live voltage, current, power, lux with animated bar indicators
- **Node status** — relay state, PIR motion, day/night mode
- **Line topology** — animated power flow between nodes, broken segment marked with ✕
- **Event log** — timestamped fault history, auto-scrolling
- **Auto-refresh** — every 15 seconds matching node TX interval

---

## 🚀 Running the Dashboard Locally

```bash
# 1. Navigate to dashboard folder
cd iss_v2

# 2. Install dependencies
pip install -r requirements.txt

# 3. Place model files in same folder
#    iss_fault_model.pkl
#    iss_fault_scaler.pkl

# 4. Run
python app.py

# 5. Open browser
# http://127.0.0.1:5000
```

---


---

## 🗺️ Roadmap

- [x] STM32 firmware — all sensors + LoRa TX working
- [x] ESP32 gateway — LoRa RX → Firebase
- [x] Firebase Realtime DB — live data confirmed
- [x] Random Forest fault detection — 99.2% accuracy
- [x] Flask live dashboard — real data + predictions
- [x] Line topology visualization
- [ ] Node 02 bring-up
- [ ] Cross-node line fault localization (lineman tool)
- [ ] Retrain model on real sensor data
- [ ] MQTT alert layer — Telegram notification on fault
- [ ] emlearn — on-device inference on STM32

---

## 👨‍💻 Author

**Shivasurya K A**
Pre-final Year · Electrical & Electronics Engineering
Knowledge Institute of Technology (KIOT), Salem, India

---

## 📄 License

This project is licensed under the MIT License — see [LICENSE](LICENSE) for details.
