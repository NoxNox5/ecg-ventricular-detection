/*
 * Single-Lead ECG Device for Detection of Ventricular Pre-Excitation
 * ===================================================================
 * EE445 Group Project B — Dublin City University, April 2024
 * Authors: Dona Dilini Wijetunge Arachchige, Colin Cunning, Subhankar Tripathi
 *
 * My contribution (Subhankar): firmware development and adaptation of the
 * Pan-Tompkins QRS detection algorithm from Arduino to MSP432 platform,
 * plus MQTT integration with ThingsBoard Cloud.
 *
 * Hardware:
 *   - MSP432P401R microcontroller (Texas Instruments)
 *   - AD8232 single-lead ECG heart rate monitor (SparkFun)
 *   - CC3100 Wi-Fi BoosterPack (stacked on MSP432)
 *
 * Algorithm:
 *   QRS detection based on the Pan-Tompkins method — high-pass filter,
 *   low-pass filter, adaptive thresholding, QRS decision.
 *   Ventricular pre-excitation is flagged when RR interval < 0.6s.
 *
 * Cloud:
 *   R-R interval published to ThingsBoard Cloud via MQTT (PubSubClient).
 *   Alarm rule on ThingsBoard fires when time < 0.6s.
 *
 * Dependencies (Energia libraries):
 *   - WiFi (CC3100 driver)
 *   - PubSubClient (MQTT)
 */

#include <WiFi.h>
#include <PubSubClient.h>

// ── Pin definitions ──────────────────────────────────────────────────────────
#define ECG_PIN     A0   // AD8232 OUTPUT → MSP432 A0
#define LO_PLUS     6    // AD8232 LO+    → MSP432 pin 6
#define LO_MINUS    8    // AD8232 LO-    → MSP432 pin 8
#define LED_PIN     RED_LED

// ── Wi-Fi / MQTT credentials ─────────────────────────────────────────────────
const char* ssid       = "YOUR_WIFI_SSID";
const char* password   = "YOUR_WIFI_PASSWORD";
const char* mqttServer = "demo.thingsboard.io";
const int   mqttPort   = 1883;
const char* mqttToken  = "YOUR_DEVICE_ACCESS_TOKEN";  // ThingsBoard device token

// ── QRS detection / BPM buffer ───────────────────────────────────────────────
#define SAMPLE_PERIOD_US  5000   // 200 Hz sample rate
#define BPM_BUFFER_SIZE   10
#define MAX_BPM           220

float bpm_buff[BPM_BUFFER_SIZE] = {0};
int   bpm_buff_WR_idx = 0;
int   bpm_buff_RD_idx = 0;
float bpm = 0;

unsigned long foundTimeMicros     = 0;
unsigned long old_foundTimeMicros = 0;
unsigned long lastSampleMicros    = 0;

// Pan-Tompkins filter state
#define PT_BUFFER_SIZE 32
int ptBuffer[PT_BUFFER_SIZE] = {0};
int ptBufferIdx = 0;
int hpFiltered  = 0;
int lpFiltered  = 0;

// Adaptive threshold state
int   next_eval_pt    = 0;
int   threshold_val   = 0;
bool  QRS_detected    = false;
int   windowMax       = 0;
int   windowCount     = 0;
#define WINDOW_SIZE 40

// ── MQTT / Wi-Fi clients ──────────────────────────────────────────────────────
WiFiClient   wifiClient;
PubSubClient client(wifiClient);

// ── Function prototypes ───────────────────────────────────────────────────────
bool  detectQRS(int ecgSample);
void  publish(float timeperiod);
void  connectToMQTT();
void  callback(char* topic, byte* payload, unsigned int length);

// ─────────────────────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);

  pinMode(LO_PLUS,  INPUT);
  pinMode(LO_MINUS, INPUT);
  pinMode(LED_PIN,  OUTPUT);
  digitalWrite(LED_PIN, LOW);

  Serial.print("Connecting to Wi-Fi");
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWi-Fi connected. IP: " + WiFi.localIP().toString());

  client.setServer(mqttServer, mqttPort);
  client.setCallback(callback);
  connectToMQTT();

  lastSampleMicros = micros();
}

// ─────────────────────────────────────────────────────────────────────────────
void loop() {
  if (!client.connected()) connectToMQTT();
  client.loop();

  if ((micros() - lastSampleMicros) < SAMPLE_PERIOD_US) return;
  lastSampleMicros = micros();

  if (digitalRead(LO_PLUS) == 1 || digitalRead(LO_MINUS) == 1) {
    Serial.println("!");
    return;
  }

  int ecgValue = analogRead(ECG_PIN);
  QRS_detected = detectQRS(ecgValue);

  if (QRS_detected) {
    foundTimeMicros = micros();
    digitalWrite(LED_PIN, HIGH);

    float timeperiod = (float)(foundTimeMicros - old_foundTimeMicros) / 1000000.0;
    Serial.println(timeperiod);
    publish(timeperiod);

    bpm_buff[bpm_buff_WR_idx] = 60.0 / ((float)(foundTimeMicros - old_foundTimeMicros) / 1000000.0);
    bpm_buff_WR_idx = (bpm_buff_WR_idx + 1) % BPM_BUFFER_SIZE;

    bpm += bpm_buff[bpm_buff_RD_idx];
    int tmp = bpm_buff_RD_idx - BPM_BUFFER_SIZE + 1;
    if (tmp < 0) tmp += BPM_BUFFER_SIZE;
    bpm -= bpm_buff[tmp];
    bpm_buff_RD_idx = (bpm_buff_RD_idx + 1) % BPM_BUFFER_SIZE;

    old_foundTimeMicros = foundTimeMicros;
    analogWrite(22, (int)((bpm / MAX_BPM) * 255));
  } else {
    digitalWrite(LED_PIN, LOW);
  }
}

// ─────────────────────────────────────────────────────────────────────────────
bool detectQRS(int ecgSample) {
  // High-pass filter (circular buffer, DC removal)
  ptBuffer[ptBufferIdx] = ecgSample;
  int hpSum = 0;
  for (int i = 0; i < PT_BUFFER_SIZE; i++) hpSum += ptBuffer[i];
  hpFiltered = ecgSample - (hpSum / PT_BUFFER_SIZE);
  ptBufferIdx = (ptBufferIdx + 1) % PT_BUFFER_SIZE;

  // Low-pass filter (simple IIR)
  lpFiltered = (lpFiltered + hpFiltered) / 2;

  // Adaptive threshold window
  next_eval_pt = lpFiltered;
  if (next_eval_pt > windowMax) windowMax = next_eval_pt;
  if (++windowCount >= WINDOW_SIZE) {
    threshold_val = windowMax * 0.6;
    windowMax = 0;
    windowCount = 0;
  }

  return (next_eval_pt > threshold_val && threshold_val > 0);
}

// ─────────────────────────────────────────────────────────────────────────────
void publish(float timeperiod) {
  String jsonPayload = "{\"time\":" + String(timeperiod) + "}";
  if (client.publish("v1/devices/me/telemetry", jsonPayload.c_str())) {
    Serial.println("ECG data published to ThingsBoard");
  } else {
    Serial.println("Failed to publish ECG data");
  }
  delay(1000);
}

// ─────────────────────────────────────────────────────────────────────────────
void connectToMQTT() {
  while (!client.connected()) {
    Serial.print("Connecting to MQTT...");
    if (client.connect("MSP432_ECG", mqttToken, NULL)) {
      Serial.println("connected.");
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" retrying in 5s");
      delay(5000);
    }
  }
}

// ─────────────────────────────────────────────────────────────────────────────
void callback(char* topic, byte* payload, unsigned int length) {
  // Not used — device is publish-only in this application
}
