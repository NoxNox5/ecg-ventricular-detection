# Single-Lead ECG Device for Ventricular Pre-Excitation Detection

**EE445 Group Project B — Dublin City University, April 2024**
*Dona Dilini Wijetunge Arachchige · Colin Cunning · Subhankar Tripathi*

An IoT-based cardiac monitoring device that detects ventricular pre-excitation in real time using a single-lead ECG setup, the Pan-Tompkins algorithm for QRS detection, and MQTT telemetry to ThingsBoard Cloud.

---

## Background

Ventricular pre-excitation is a cardiac condition where electrical impulses reach the ventricles earlier than expected via an abnormal pathway, bypassing the AV node. It can cause life-threatening arrhythmias, particularly in young athletes, and is one of the leading causes of sudden cardiac death in under-35s.

The key indicator we focused on is a **shortened PR interval** — specifically an RR interval below 0.6 seconds — which can be reliably detected with a single-lead ECG. If the device records a value under that threshold, it fires an alarm on the ThingsBoard dashboard.

The reason we went single-lead was accessibility: simpler setup, lower cost, and something wearable. It's not a replacement for clinical-grade 12-lead ECG, but as a screening tool it covers a lot of ground.

---

## Hardware

| Component | Part | Purpose |
|---|---|---|
| Microcontroller | MSP432P401R (Texas Instruments) | Processing + ADC |
| ECG Sensor | AD8232 (SparkFun) | Single-lead ECG acquisition |
| Wi-Fi Module | CC3100 BoosterPack | MQTT over Wi-Fi |
| Electrodes | 3 adhesive pads | RA, LA, RL placement |

**Wiring (AD8232 → MSP432):**
- OUTPUT → A0 (ECG signal)
- LO+ → Pin 6 (leads-off detect)
- LO- → Pin 8 (leads-off detect)
- 3.3V, GND as labelled

The CC3100 Wi-Fi module stacks directly on top of the MSP432 (BoosterPack form factor).

---

## How it works

```
AD8232 Sensor
     │  (analog ECG)
     ▼
MSP432P401R
  ├── High-pass filter  (removes baseline wander)
  ├── Low-pass filter   (removes high-freq noise)
  ├── Adaptive thresholding (Pan-Tompkins inspired)
  └── QRS detection → RR interval calculation
          │
          ▼ (MQTT publish via CC3100)
ThingsBoard Cloud
  ├── Real-time R-R interval chart
  └── Alarm: fires if R-R interval < 0.6s → Ventricular Pre-Excitation flag
```

**Ventricular pre-excitation detection logic:**
Two consecutive QRS detections give the RR interval. Normal range is 0.6–1.2s. Below 0.6s triggers the cloud alarm on the ThingsBoard dashboard.

---

## Algorithm

The QRS detection is based on the Pan-Tompkins method, adapted from an Arduino implementation to run on the MSP432 in Energia. The original differentiator and squaring stages were simplified in favour of an adaptive threshold approach — less computationally expensive on the MSP, and accurate enough in practice.

Steps in the `detectQRS()` function:
1. **Circular buffer high-pass filter** — removes DC offset and baseline wander
2. **IIR low-pass filter** — smooths the signal
3. **Rolling window maximum** — tracks signal envelope
4. **Adaptive threshold** — set at 60% of window max, updates every 40 samples
5. **Decision** — QRS flagged when signal exceeds threshold

---

## MQTT / ThingsBoard Setup

1. Create a free account at [ThingsBoard Cloud](https://thingsboard.cloud)
2. Add a new device — copy the access token
3. Create a dashboard with:
   - Time-series widget for `time` key (R-R interval)
   - Alarm rule: `time < 0.6` → severity Critical, type VPE
4. Update `mqttToken` and Wi-Fi credentials in the sketch

Published payload format:
```json
{"time": 0.73}
```

---

## Setup & Flash

**Requirements:**
- [Energia IDE](https://energia.nu/) (Arduino-compatible for MSP432)
- `WiFi` library (CC3100 driver, bundled with Energia)
- `PubSubClient` library (install via Energia Library Manager)

**Steps:**
1. Open `src/ecg_vpre_detection.ino` in Energia
2. Set your Wi-Fi credentials and ThingsBoard device token (lines ~21–24)
3. Select board: **MSP-EXP432P401R**
4. Flash and open Serial Monitor at 115200 baud
5. You should see RR intervals printing and ThingsBoard receiving data

---

## Results

The device successfully acquired real ECG waveforms from team members and published RR intervals to ThingsBoard continuously. Intervals ranged from ~0.67–0.83s (healthy range, no pre-excitation). Alarm functionality was validated by manually injecting a value < 0.6 via curl.

---

## My contribution

Firmware development — specifically adapting the Pan-Tompkins QRS detection code from the Arduino platform to MSP432 (pin mapping, ADC configuration, timing), and integrating the MQTT publish pipeline with ThingsBoard. Colin and Dona handled hardware connections and cloud dashboard setup respectively.

---

## Tech Stack

![C++](https://img.shields.io/badge/C++-00599C?style=flat-square&logo=cplusplus&logoColor=white)
![Energia](https://img.shields.io/badge/Energia%20IDE-MSP432-red?style=flat-square)
![MQTT](https://img.shields.io/badge/MQTT-3C5280?style=flat-square&logo=eclipsemosquitto&logoColor=white)
![ThingsBoard](https://img.shields.io/badge/ThingsBoard-Cloud-brightgreen?style=flat-square)

---

*EE445 Bioelectronics — Dublin City University, 2024*
