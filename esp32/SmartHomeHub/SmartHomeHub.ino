/*
 * Smart Home Hub - ESP32
 * ----------------------
 * - Reads temperature/humidity from a DHT22 every minute and uploads it to Firebase Realtime Database
 * - Polls Firebase every 2 seconds for the relay command (/hub/relay) and switches the relay
 * - Reports the actual relay state back to (/hub/relayState)
 * - Auth: Firebase Auth (email/password) over REST, no Firebase library needed
 *
 * Required libraries (Library Manager):
 *   - "DHT sensor library" by Adafruit
 *   - "Adafruit Unified Sensor"
 *
 * Copy secrets.h.example to secrets.h and fill in your own values.
 */

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <DHT.h>
#include "secrets.h"

// ---------- Pins ----------
#define DHT_PIN    4
#define DHT_TYPE   DHT22
#define RELAY_PIN  5
// Most common relay modules are active-LOW. Set to false for an active-HIGH module.
#define RELAY_ACTIVE_LOW true

// ---------- Timing ----------
const unsigned long SENSOR_INTERVAL_MS = 60UL * 1000UL;        // every minute
const unsigned long RELAY_POLL_MS      = 2000UL;               // every 2 seconds
const unsigned long TOKEN_MARGIN_MS    = 5UL * 60UL * 1000UL;  // refresh token 5 min before expiry

DHT dht(DHT_PIN, DHT_TYPE);

String idToken = "";
unsigned long tokenExpiresAt = 0;   // millis()
unsigned long lastSensor = 0;
unsigned long lastPoll = 0;
bool relayOn = false;

// ---------- Helpers ----------
void setRelay(bool on) {
  relayOn = on;
  bool level = RELAY_ACTIVE_LOW ? !on : on;
  digitalWrite(RELAY_PIN, level ? HIGH : LOW);
}

// Extract a string value from simple JSON: "key": "value"
String jsonString(const String &json, const char *key) {
  String pattern = String("\"") + key + "\"";
  int k = json.indexOf(pattern);
  if (k < 0) return "";
  int colon = json.indexOf(':', k);
  int q1 = json.indexOf('"', colon + 1);
  int q2 = json.indexOf('"', q1 + 1);
  if (colon < 0 || q1 < 0 || q2 < 0) return "";
  return json.substring(q1 + 1, q2);
}

void connectWiFi() {
  if (WiFi.status() == WL_CONNECTED) return;
  Serial.printf("[WiFi] Connecting to %s", WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 20000) {
    delay(500);
    Serial.print(".");
  }
  Serial.println(WiFi.status() == WL_CONNECTED ? " OK" : " FAILED");
}

// ---------- Firebase Auth ----------
bool signIn() {
  WiFiClientSecure client;
  client.setInsecure();   // OK for prototyping. For production use client.setCACert(...)
  HTTPClient http;
  String url = String("https://identitytoolkit.googleapis.com/v1/accounts:signInWithPassword?key=") + FIREBASE_API_KEY;
  if (!http.begin(client, url)) return false;
  http.addHeader("Content-Type", "application/json");
  String body = String("{\"email\":\"") + DEVICE_EMAIL + "\",\"password\":\"" + DEVICE_PASSWORD +
                "\",\"returnSecureToken\":true}";
  int code = http.POST(body);
  String resp = http.getString();
  http.end();

  if (code != 200) {
    Serial.printf("[Auth] Failed (%d): %s\n", code, resp.c_str());
    return false;
  }
  idToken = jsonString(resp, "idToken");
  long expiresIn = jsonString(resp, "expiresIn").toInt();   // seconds (usually 3600)
  if (expiresIn <= 0) expiresIn = 3600;
  tokenExpiresAt = millis() + (unsigned long)expiresIn * 1000UL;
  Serial.println("[Auth] Signed in");
  return idToken.length() > 0;
}

bool ensureAuth() {
  if (idToken.length() == 0 || (long)(tokenExpiresAt - TOKEN_MARGIN_MS - millis()) <= 0) {
    return signIn();
  }
  return true;
}

// ---------- Firebase RTDB (REST) ----------
String dbUrl(const char *path) {
  return String(FIREBASE_DB_URL) + path + ".json?auth=" + idToken;
}

bool dbPatch(const char *path, const String &json) {
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  if (!http.begin(client, dbUrl(path))) return false;
  http.addHeader("Content-Type", "application/json");
  int code = http.PATCH(json);
  http.end();
  if (code != 200) Serial.printf("[DB] PATCH %s failed (%d)\n", path, code);
  return code == 200;
}

// Returns: 1 = true, 0 = false, -1 = error / no value
int dbGetBool(const char *path) {
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  if (!http.begin(client, dbUrl(path))) return -1;
  int code = http.GET();
  String body = http.getString();
  http.end();
  if (code != 200) {
    Serial.printf("[DB] GET %s failed (%d)\n", path, code);
    return -1;
  }
  body.trim();
  if (body == "true") return 1;
  if (body == "false") return 0;
  return -1;
}

// ---------- Logic ----------
void publishSensor() {
  float t = dht.readTemperature();
  float h = dht.readHumidity();
  if (isnan(t) || isnan(h)) {
    Serial.println("[DHT] Read failed");
    return;
  }
  Serial.printf("[DHT] %.1f C, %.1f %%\n", t, h);
  String json = String("{\"temperature\":") + String(t, 1) +
                ",\"humidity\":" + String(h, 1) +
                ",\"updatedAt\":{\".sv\":\"timestamp\"}}";
  dbPatch("/hub", json);
}

void pollRelay() {
  int cmd = dbGetBool("/hub/relay");
  if (cmd < 0) return;
  bool want = (cmd == 1);
  if (want != relayOn) {
    setRelay(want);
    Serial.printf("[Relay] -> %s\n", want ? "ON" : "OFF");
    dbPatch("/hub", String("{\"relayState\":") + (want ? "true" : "false") + "}");
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(RELAY_PIN, OUTPUT);
  setRelay(false);          // relay is OFF at boot
  dht.begin();
  connectWiFi();
  if (WiFi.status() == WL_CONNECTED && ensureAuth()) {
    dbPatch("/hub", "{\"relayState\":false}");
    publishSensor();
    lastSensor = millis();
  }
}

void loop() {
  connectWiFi();
  if (WiFi.status() != WL_CONNECTED) { delay(2000); return; }
  if (!ensureAuth()) { delay(5000); return; }

  unsigned long now = millis();

  if (now - lastPoll >= RELAY_POLL_MS) {
    lastPoll = now;
    pollRelay();
  }
  if (now - lastSensor >= SENSOR_INTERVAL_MS) {
    lastSensor = now;
    publishSensor();
  }
  delay(50);
}
