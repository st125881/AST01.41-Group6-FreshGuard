#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <DHT.h>
#include <time.h>

// ---- EDIT ----
const char* WIFI_SSID     = "2GSV63A2";
const char* WIFI_PASS     = "7fMm5CGf";
const char* CLOUD_URL     = "https://freshguardnr.iotcloud-lab.com/freshguard/ingest";
const char* CLOUD_API_KEY = "R+7$&%Lq5(0f5(%Ppn=qiW4EUegc)2+p";
const bool  ACTIVE_LOW    = true;        
const float LED_TEMP_THRESHOLD = 10.0;   
// --------------

#define DHT_PIN 4
#define LED3_PIN 5
const char* UNIT_ID = "fridge01";

const uint32_t SAMPLE_MS = 5000;                  
const unsigned long CLOUD_INTERVAL_MS = 10000UL;  

DHT dht(DHT_PIN, DHT22);
uint32_t lastSample = 0;
unsigned long lastCloudAttempt = 0;

float lastTemp = NAN;
float lastHum  = NAN;
bool  lastLedState = false;


static const char CLOUD_ROOT_CA[] = R"PEM(
-----BEGIN CERTIFICATE-----
MIICGzCCAaGgAwIBAgIQQdKd0XLq7qeAwSxs6S+HUjAKBggqhkjOPQQDAzBPMQsw
CQYDVQQGEwJVUzEpMCcGA1UEChMgSW50ZXJuZXQgU2VjdXJpdHkgUmVzZWFyY2gg
R3JvdXAxFTATBgNVBAMTDElTUkcgUm9vdCBYMjAeFw0yMDA5MDQwMDAwMDBaFw00
MDA5MTcxNjAwMDBaME8xCzAJBgNVBAYTAlVTMSkwJwYDVQQKEyBJbnRlcm5ldCBT
ZWN1cml0eSBSZXNlYXJjaCBHcm91cDEVMBMGA1UEAxMMSVNSRyBSb290IFgyMHYw
EAYHKoZIzj0CAQYFK4EEACIDYgAEzZvVn4CDCuwJSvMWSj5cz3es3mcFDR0HttwW
+1qLFNvicWDEukWVEYmO6gbf9yoWHKS5xcUy4APgHoIYOIvXRdgKam7mAHf7AlF9
ItgKbppbd9/w+kHsOdx1ymgHDB/qo0IwQDAOBgNVHQ8BAf8EBAMCAQYwDwYDVR0T
AQH/BAUwAwEB/zAdBgNVHQ4EFgQUfEKWrt5LSDv6kviejM9ti6lyN5UwCgYIKoZI
zj0EAwMDaAAwZQIwe3lORlCEwkSHRhtFcP9Ymd70/aTSVaYgLXTWNLxBo1BfASdW
tL4ndQavEi51mI38AjEAi/V3bNTIZargCyzuFJ0nN6T5U6VR5CmD1/iQMVtCnwr1
/q4AaOeMSQ+2b1tbFfLn
-----END CERTIFICATE-----
)PEM";

void ledWrite(bool on) {
  digitalWrite(LED3_PIN, (on != ACTIVE_LOW) ? HIGH : LOW);
}

void connectWiFi() {
  if (WiFi.status() == WL_CONNECTED) return;
  Serial.print("WiFi");
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.print("\nESP32 IP: ");
  Serial.println(WiFi.localIP());
}

String buildPayload(float t, float h, bool ledOn) {
  String json;
  json.reserve(220);
  json = "{\"unit_id\":\"";
  json += UNIT_ID;
  json += "\",\"data_source\":\"real\",\"temperature\":";
  json += String(t, 2);
  json += ",\"humidity\":";
  json += String(h, 2);
  json += ",\"led_on\":";
  json += (ledOn ? "true" : "false");
  json += "}";
  return json;
}

bool sendToCloud(float t, float h, bool ledOn) {
  // ต้อง sync เวลาก่อน เพราะ HTTPS ต้องตรวจวันหมดอายุของ certificate
  time_t now = time(nullptr);
  if (now < 1735689600) {  // เช็คว่าผ่านปี 2025 แล้วหรือยัง
    Serial.println("[CLOUD] Waiting for NTP time sync");
    configTime(0, 0, "pool.ntp.org", "time.google.com");
    return false;
  }

  WiFiClientSecure secureClient;
  secureClient.setCACert(CLOUD_ROOT_CA);

  HTTPClient https;
  https.setConnectTimeout(4000);
  https.setTimeout(5000);

  if (!https.begin(secureClient, CLOUD_URL)) {
    Serial.println("[CLOUD] HTTPS init failed");
    return false;
  }

  https.addHeader("Content-Type", "application/json");
  https.addHeader("x-freshguard-key", CLOUD_API_KEY);

  String payload = buildPayload(t, h, ledOn);
  int statusCode = https.POST(payload);

  bool ok = false;
  if (statusCode == 202) {
    Serial.println("[CLOUD] Accepted (202)");
    ok = true;
  } else if (statusCode > 0) {
    Serial.printf("[CLOUD] HTTP status: %d\n", statusCode);
    Serial.println(https.getString());
  } else {
    Serial.printf("[CLOUD] Request failed: %s\n", https.errorToString(statusCode).c_str());
  }

  https.end();
  return ok;
}

void setup() {
  Serial.begin(115200);
  pinMode(LED3_PIN, OUTPUT);
  dht.begin();

  connectWiFi();

  
  configTime(0, 0, "pool.ntp.org", "time.google.com");
}

void loop() {
  connectWiFi();

  
  if (millis() - lastSample >= SAMPLE_MS) {
    lastSample = millis();

    float t = dht.readTemperature();
    float h = dht.readHumidity();

    if (isnan(t) || isnan(h)) {
      Serial.println("DHT22 read failed");
    } else {
      lastTemp = t;
      lastHum  = h;
      lastLedState = (t > LED_TEMP_THRESHOLD);
      ledWrite(lastLedState);
      Serial.printf("Read T=%.1f H=%.1f LED=%s\n", t, h, lastLedState ? "ON" : "OFF");
    }
  }

  
  if (millis() - lastCloudAttempt >= CLOUD_INTERVAL_MS) {
    lastCloudAttempt = millis();

    if (!isnan(lastTemp) && !isnan(lastHum)) {
      bool ok = sendToCloud(lastTemp, lastHum, lastLedState);
      Serial.printf("Sent T=%.1f H=%.1f LED=%s (%s)\n",
                     lastTemp, lastHum, lastLedState ? "ON" : "OFF", ok ? "OK" : "FAILED");
    } else {
      Serial.println("[CLOUD] No valid sensor data to send yet");
    }
  }
}