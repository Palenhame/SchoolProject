#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <Wire.h>
#include <MPU6050_light.h>

// ---------------- WiFi ----------------
const char* ssid = "Redmi_A209";
const char* password = "Ar020709";

// ---------------- Server URLs ----------------
const char* urlX = "http://braselet.kzhivaev.ru/api/use/activate/0";
const char* urlZ = "http://braselet.kzhivaev.ru/api/use/activate/1";

// ---------------- MPU6050 ----------------
MPU6050 mpu(Wire);
bool mpuOk = false;

// ---------------- Detect config ----------------
const float ACCEL_THRESHOLD = 1.5;   // g
const float RESET_FACTOR   = 0.6;    // гистерезис
const unsigned long COOLDOWN = 3000; // ms между событиями
unsigned long lastTriggerTime = 0;

// ---------------- Axis state ----------------
bool xActive = false;
bool zActive = false;

// ---------------- MPU reconnect ----------------
unsigned long lastMpuCheck = 0;
const unsigned long MPU_CHECK_INTERVAL = 1000; // ms

// ---------------- Serial debug ----------------
unsigned long lastPrint = 0;
const unsigned long PRINT_INTERVAL = 1000; // 1 секунда
unsigned long printCounter = 0;

// ---------------- WiFi Management ----------------
bool wifiConnected = false;
unsigned long lastWiFiCheck = 0;
const unsigned long WIFI_CHECK_INTERVAL = 1000; // ms
unsigned long wifiConnectStart = 0;
const unsigned long WIFI_CONNECT_TIMEOUT = 15000;
int wifiDisconnectCount = 0;
const int MAX_DISCONNECTS_BEFORE_RESTART = 5;

// ---------------- HTTP status ----------------
int lastHTTPCode = 0;
bool httpInProgress = false;
unsigned long httpStartTime = 0;

// ---------------- Оптимизация ----------------
unsigned long lastMPURead = 0;
const unsigned long MPU_READ_INTERVAL = 50; // Читаем MPU каждые 50 мс
float lastAX = 0, lastAZ = 0;

// ------------------------------------------------
void initMPU() {
  Serial.print("MPU init... ");
  if (mpu.begin() == 0) {
    Serial.println("OK");
    mpu.calcOffsets();
    Serial.println("MPU calibrated");
    mpuOk = true;
  } else {
    Serial.println("FAILED");
    mpuOk = false;
  }
}

// ------------------------------------------------
void setupWiFi() {
  Serial.println("\n[WiFi] Initializing...");
  
  WiFi.mode(WIFI_STA);
  wifi_set_sleep_type(NONE_SLEEP_T);
  WiFi.setSleepMode(WIFI_NONE_SLEEP);
  WiFi.setAutoReconnect(true);
  WiFi.persistent(true);
  
  Serial.print("Connecting to: ");
  Serial.println(ssid);
  
  WiFi.begin(ssid, password);
  wifiConnectStart = millis();
}

// ------------------------------------------------
void manageWiFi() {
  unsigned long now = millis();
  
  if (now - lastWiFiCheck < WIFI_CHECK_INTERVAL) return;
  lastWiFiCheck = now;
  
  wl_status_t status = WiFi.status();
  
  if (status == WL_CONNECTED) {
    if (!wifiConnected) {
      wifiConnected = true;
      Serial.println("[WiFi] Connected!");
      Serial.print("IP: "); Serial.println(WiFi.localIP());
      Serial.print("RSSI: "); Serial.println(WiFi.RSSI());
      
      if (wifiDisconnectCount > 0) {
        wifiDisconnectCount = 0;
      }
    }
  } else {
    if (wifiConnected) {
      wifiConnected = false;
      wifiDisconnectCount++;
      Serial.print("[WiFi] Disconnected! Count: ");
      Serial.println(wifiDisconnectCount);
    }
    
    if (now - wifiConnectStart > WIFI_CONNECT_TIMEOUT) {
      Serial.println("[WiFi] Timeout, restarting...");
      ESP.restart();
    }
    
    static unsigned long lastReconnectAttempt = 0;
    if (now - lastReconnectAttempt > 3000) {
      lastReconnectAttempt = now;
      Serial.println("[WiFi] Reconnecting...");
      
      if (wifiDisconnectCount < 3) {
        WiFi.reconnect();
      } else {
        WiFi.disconnect();
        delay(10);
        WiFi.begin(ssid, password);
        wifiConnectStart = now;
      }
    }
    
    if (wifiDisconnectCount >= MAX_DISCONNECTS_BEFORE_RESTART) {
      Serial.println("[WiFi] Too many disconnects, restarting...");
      delay(100);
      ESP.restart();
    }
  }
}

// ------------------------------------------------
bool testServerConnection() {
  Serial.print("[TEST] Testing server connection... ");
  
  WiFiClient client;
  HTTPClient http;
  
  // Пробуем короткий GET запрос для проверки
  http.begin(client, "http://braselet.kzhivaev.ru/");
  http.setTimeout(3000);
  
  unsigned long start = millis();
  int httpCode = http.GET();
  unsigned long duration = millis() - start;
  http.end();
  
  if (httpCode > 0) {
    Serial.print("OK (");
    Serial.print(httpCode);
    Serial.print(") in ");
    Serial.print(duration);
    Serial.println("ms");
    return true;
  } else {
    Serial.print("FAILED: ");
    Serial.print(HTTPClient::errorToString(httpCode));
    Serial.print(" (");
    Serial.print(duration);
    Serial.println("ms)");
    
    // Проверяем DNS
    Serial.print("[TEST] DNS test... ");
    IPAddress ip;
    if (WiFi.hostByName("braselet.kzhivaev.ru", ip)) {
      Serial.print("OK: ");
      Serial.println(ip);
    } else {
      Serial.println("FAILED");
    }
    
    return false;
  }
}

// ------------------------------------------------
void setup() {
  Serial.begin(115200);
  delay(300);
  
  Serial.println("\n==========================================");
  Serial.println("ESP8266 MPU6050 - EVENT ONLY");
  Serial.println("Sends POST only on X/Z axis events");
  Serial.println("==========================================");
  
  Wire.begin();
  initMPU();
  setupWiFi();
}

// ------------------------------------------------
void loop() {
  unsigned long now = millis();
  
  // 1. Управление WiFi (приоритет)
  manageWiFi();
  
  // 2. Проверка MPU (редко)
  if (!mpuOk && now - lastMpuCheck > MPU_CHECK_INTERVAL) {
    lastMpuCheck = now;
    initMPU();
    return;
  }
  
  if (!mpuOk) return;
  
  // 3. Чтение MPU с ограниченной частотой
  if (now - lastMPURead >= MPU_READ_INTERVAL) {
    lastMPURead = now;
    
    mpu.update();
    lastAX = mpu.getAccX();
    lastAZ = mpu.getAccZ() - 1.0;
    
    // Вывод данных раз в секунду
    if (now - lastPrint >= PRINT_INTERVAL) {
      lastPrint = now;
      printCounter++;
      
      if (printCounter % 10 == 0) {
        Serial.print("\n[STATUS] Time:");
        Serial.print(now/1000);
        Serial.print("s WiFi:");
        Serial.print(wifiConnected ? "YES" : "NO");
        Serial.print(" RSSI:");
        Serial.print(WiFi.RSSI());
        Serial.print(" LastHTTP:");
        Serial.print(lastHTTPCode);
        Serial.print(" Heap:");
        Serial.print(ESP.getFreeHeap());
        Serial.println();
      }
      
      Serial.print("AX=");
      Serial.print(lastAX, 2);
      Serial.print(" AZ=");
      Serial.print(lastAZ, 2);
      Serial.print(" | WiFi:");
      Serial.print(wifiConnected ? "OK" : "NO");
      if (wifiConnected) {
        Serial.print("(");
        Serial.print(WiFi.RSSI());
        Serial.print(")");
      }
      if (lastHTTPCode != 0) {
        Serial.print(" | HTTP:");
        Serial.print(lastHTTPCode);
      }
      Serial.println();
    }
    
    // 4. Проверка событий MPU
    if (now - lastTriggerTime >= COOLDOWN) {
      
      if (!xActive && abs(lastAX) > ACCEL_THRESHOLD) {
        Serial.print(">>> X EVENT: ");
        Serial.println(lastAX, 2);
        
        if (wifiConnected && !httpInProgress) {
          sendEvent(urlX, "X");
        } else {
          Serial.println("Skipped - WiFi/HTTP busy");
        }
        
        xActive = true;
        lastTriggerTime = now;
      } else if (xActive && abs(lastAX) < ACCEL_THRESHOLD * RESET_FACTOR) {
        xActive = false;
      }
      
      if (!zActive && abs(lastAZ) > ACCEL_THRESHOLD) {
        Serial.print(">>> Z EVENT: ");
        Serial.println(lastAZ, 2);
        
        if (wifiConnected && !httpInProgress) {
          sendEvent(urlZ, "Z");
        } else {
          Serial.println("Skipped - WiFi/HTTP busy");
        }
        
        zActive = true;
        lastTriggerTime = now;
      } else if (zActive && abs(lastAZ) < ACCEL_THRESHOLD * RESET_FACTOR) {
        zActive = false;
      }
    }
  }
  
  // Короткая пауза
  delay(1);
}

// ------------------------------------------------
void sendEvent(const char* url, const char* axis) {
  if (!wifiConnected || httpInProgress) return;
  
  // Сначала проверяем доступность сервера
  if (!testServerConnection()) {
    Serial.println("[EVENT] Server not available, skipping...");
    return;
  }
  
  Serial.print("[HTTP] Sending ");
  Serial.print(axis);
  Serial.print(" to ");
  Serial.println(url);
  
  WiFiClient client;
  HTTPClient http;
  
  http.begin(client, url);
  http.setTimeout(3000);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Connection", "close");
  
  String json = "{\"axis\":\"";
  json += axis;
  json += "\",\"time\":";
  json += millis();
  json += "}";
  
  httpInProgress = true;
  httpStartTime = millis();
  
  int httpCode = http.POST(json);
  String response = http.getString();
  unsigned long duration = millis() - httpStartTime;
  
  Serial.print("Status: ");
  Serial.print(httpCode);
  
  if (httpCode > 0) {
    Serial.print(" (HTTP ");
    Serial.print(httpCode);
    Serial.print(")");
  } else {
    Serial.print(" (Error: ");
    switch(httpCode) {
      case -1: Serial.print("CONNECTION_FAILED"); break;
      case -11: Serial.print("TIMEOUT"); break;
      default: Serial.print("Code "); Serial.print(httpCode); break;
    }
    Serial.print(")");
  }
  
  Serial.print(" | Time: ");
  Serial.print(duration);
  Serial.println("ms");
  
  if (httpCode <= 0) {
    Serial.print("Error detail: ");
    Serial.println(HTTPClient::errorToString(httpCode));
  }
  
  lastHTTPCode = httpCode;
  http.end();
  httpInProgress = false;
  
  if (httpCode <= 0) {
    wifiDisconnectCount++;
    Serial.print("[WiFi] Failed, disconnect count: ");
    Serial.println(wifiDisconnectCount);
  }
}
