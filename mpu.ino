#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <Wire.h>
#include <MPU6050_light.h>

// ---------------- WiFi ----------------
const char* ssid = "Redmi_A209";
const char* password = "Ar020709";

// ---------------- Server ----------------
const char* serverIP = "192.168.31.237";
const int serverPort = 8000;

// ---------------- MPU6050 ----------------
MPU6050 mpu(Wire);

// ---------------- Timing ----------------
unsigned long lastSendTime = 0;
const unsigned long sendInterval = 10; // 100 Hz

// ------------------------------------------------

void setup() {
  Serial.begin(115200);
  delay(300);

  Serial.println();
  Serial.println("ESP8266 + MPU6050");
  Serial.println("RAW STREAM MODE");
  Serial.println("----------------");

  // ---- I2C ----
  Wire.begin();
  delay(50);

  // ---- MPU INIT ----
  Serial.print("MPU init... ");
  if (mpu.begin() != 0) {
    Serial.println("FAILED");
    ESP.restart();
  }
  Serial.println("OK");

  Serial.println("Calibrating MPU (do not move)");
  delay(1000);
  mpu.calcOffsets();
  Serial.println("Calibration done");

  // ---- WIFI INIT ----
  Serial.print("WiFi connecting");
  WiFi.begin(ssid, password);

  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED) {
    delay(150);
    Serial.print(".");
    if (millis() - start > 7000) {
      Serial.println("\nWiFi FAILED");
      ESP.restart();
    }
  }

  Serial.println("\nWiFi connected");
  Serial.print("IP: ");
  Serial.println(WiFi.localIP());

  Serial.println("Streaming started");
  Serial.println();
}

// ------------------------------------------------

void loop() {
  // 🔥 обновляем MPU максимально часто
  mpu.update();

  // ---- SEND RAW DATA ----
  if (millis() - lastSendTime >= sendInterval) {
    sendData();
    lastSendTime = millis();
  }
}

// ------------------------------------------------
// SEND RAW DATA (MINIMAL)
// ------------------------------------------------

void sendData() {
  String json = "{";
  json += "\"angle_x\":" + String(mpu.getAngleX(), 2) + ",";
  json += "\"angle_y\":" + String(mpu.getAngleY(), 2) + ",";
  json += "\"angle_z\":" + String(mpu.getAngleZ(), 2) + ",";
  json += "\"accel_x\":" + String(mpu.getAccX(), 3) + ",";
  json += "\"accel_y\":" + String(mpu.getAccY(), 3) + ",";
  json += "\"accel_z\":" + String(mpu.getAccZ(), 3);
  json += "}";

  WiFiClient client;
  HTTPClient http;

  http.begin(client,
    String("http://") + serverIP + ":" + serverPort + "/api/data"
  );
  http.addHeader("Content-Type", "application/json");
  http.POST(json);
  http.end();
}
