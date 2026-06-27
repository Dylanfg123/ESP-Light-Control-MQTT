#include <WiFi.h>
#include <ArduinoOTA.h>
#include <PubSubClient.h>
#include <FastLED.h>
#include <ctype.h>
#include "secrets.h"

// ——— CONFIG ———
const char* MQTT_SERVER = "192.168.0.23";
const int   MQTT_PORT   = 1883;
const char* MQTT_TOPIC  = "led/control";
const uint32_t WIFI_RETRY_MS = 10000;
const uint32_t MQTT_RETRY_MS = 2000;
const uint32_t NETWORK_TIMEOUT_MS = 2000;
const size_t COMMAND_BUFFER_SIZE = 32;

// ——— LED SETUP ———
#define DATA_PIN    13
#define NUM_LEDS    160
CRGB leds[NUM_LEDS];

// brightness steps (4 levels)
const uint8_t brightnessLevels[] = { 64, 128, 192, 255 };
uint8_t brightnessIndex = 3;  // start at 255

// static‐color list
const CRGB staticColorsList[] = {
  CRGB::White,                 // index 0
  CRGB(255,223,127),           // Sun (warm white) index 1
  CRGB(255,80,10),             // Warm orange index 2
  CRGB::Blue,                  // Blue index 3
  CRGB::Red,                   // Red index 4
  CRGB::Green                  // Green index 5
};
const uint8_t NUM_STATIC_COLORS = sizeof(staticColorsList) / sizeof(staticColorsList[0]);
uint8_t staticColorIndex = 1;           // start at Sun
CRGB   staticColor      = staticColorsList[staticColorIndex];

// modes
enum Mode { OFF, STATIC, RAINBOW };
Mode currentMode = OFF;

// state tracking
bool    stripDirty = false;  // needs a one‐time update
uint8_t rainbowHue = 0;

// Wi-Fi & MQTT
class TimedWiFiClient : public WiFiClient {
 public:
  int connect(IPAddress ip, uint16_t port) override {
    return WiFiClient::connect(ip, port, NETWORK_TIMEOUT_MS);
  }

  int connect(const char* host, uint16_t port) override {
    return WiFiClient::connect(host, port, NETWORK_TIMEOUT_MS);
  }
};

TimedWiFiClient wifiClient;
PubSubClient mqtt(wifiClient);
char mqttClientId[32];
char otaHostname[32];
bool otaStarted = false;

// single routine to perform the actual LED update (OFF or STATIC)
void updateStrip() {
  FastLED.setBrightness(brightnessLevels[brightnessIndex]);
  if (currentMode == OFF) {
    FastLED.clear(true);
  } else {  // STATIC
    fill_solid(leds, NUM_LEDS, staticColor);
    FastLED.show();
  }
}

// routine to update one rainbow frame
void updateRainbowFrame() {
  FastLED.setBrightness(brightnessLevels[brightnessIndex]);
  fill_rainbow(leds, NUM_LEDS, rainbowHue, 7);
  FastLED.show();
  rainbowHue++;
}

// handle incoming text commands
void handleCommand(const char* cmd) {
  if      (strcmp(cmd, "NEXTCOLOR") == 0) {
    // step through the staticColorsList
    staticColorIndex = (staticColorIndex + 1) % NUM_STATIC_COLORS;
    staticColor = staticColorsList[staticColorIndex];
    currentMode = STATIC;
  }
  else if (strcmp(cmd, "ON") == 0) {
    currentMode = STATIC;
  }
  else if (strcmp(cmd, "OFF") == 0) {
    currentMode = OFF;
  }
  else if (strcmp(cmd, "BRIGHTUP") == 0) {
    if (brightnessIndex < sizeof(brightnessLevels)-1) brightnessIndex++;
  }
  else if (strcmp(cmd, "BRIGHTDOWN") == 0) {
    if (brightnessIndex > 0) brightnessIndex--;
  }
  else if (strcmp(cmd, "PARTY") == 0) {
    currentMode = RAINBOW;
  }
  else if (strcmp(cmd, "WHITE") == 0) {
    staticColorIndex = 0;
    staticColor      = staticColorsList[staticColorIndex];
    currentMode      = STATIC;
  }
  else if (strcmp(cmd, "SUN") == 0) {
    staticColorIndex = 1;
    staticColor      = staticColorsList[staticColorIndex];
    currentMode      = STATIC;
  }
  else if (strcmp(cmd, "ORANGE") == 0) {
    staticColorIndex = 2;
    staticColor      = staticColorsList[staticColorIndex];
    currentMode      = STATIC;
  }
  else if (strcmp(cmd, "BLUE") == 0) {
    staticColorIndex = 3;
    staticColor      = staticColorsList[staticColorIndex];
    currentMode      = STATIC;
  }
  else if (strcmp(cmd, "RED") == 0) {
    staticColorIndex = 4;
    staticColor      = staticColorsList[staticColorIndex];
    currentMode      = STATIC;
  }
  else if (strcmp(cmd, "GREEN") == 0) {
    staticColorIndex = 5;
    staticColor      = staticColorsList[staticColorIndex];
    currentMode      = STATIC;
  }
  else {
    Serial.printf("Unknown command: %s\n", cmd);
    return;
  }

  // mark for a one‐time refresh (OFF and STATIC)
  stripDirty = true;

  // debug
  Serial.printf("Mode=%d BrightIdx=%d ColorIdx=%d Color=%02X%02X%02X\n",
                currentMode,
                brightnessIndex,
                staticColorIndex,
                staticColor.r, staticColor.g, staticColor.b);
}

// MQTT message callback
void mqttCallback(char* topic, byte* payload, unsigned int len) {
  (void)topic;

  unsigned int start = 0;
  unsigned int end = len;
  while (start < end && isspace(static_cast<unsigned char>(payload[start]))) start++;
  while (end > start && isspace(static_cast<unsigned char>(payload[end - 1]))) end--;

  const size_t commandLength = end - start;
  if (commandLength == 0 || commandLength >= COMMAND_BUFFER_SIZE) {
    Serial.printf("Rejected MQTT command with length %u\n", len);
    return;
  }

  char cmd[COMMAND_BUFFER_SIZE];
  for (size_t i = 0; i < commandLength; i++) {
    cmd[i] = toupper(static_cast<unsigned char>(payload[start + i]));
  }
  cmd[commandLength] = '\0';

  Serial.printf("Recv CMD: %s\n", cmd);
  handleCommand(cmd);
}

void startOTA() {
  ArduinoOTA.setHostname(otaHostname);
  ArduinoOTA.setPassword(OTA_PASSWORD);

  ArduinoOTA.onStart([]() {
    Serial.println("OTA update starting");
    currentMode = OFF;
    updateStrip();
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("\nOTA update complete");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    const unsigned int percent = total == 0 ? 0 : (progress * 100U) / total;
    Serial.printf("\rOTA progress: %u%%", percent);
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("\nOTA error %u\n", error);
  });

  ArduinoOTA.begin();
  otaStarted = true;
  Serial.printf("OTA ready at %s.local\n", otaHostname);
}

// Non-blocking reconnect logic keeps LED updates running during an outage.
void maintainConnections() {
  static uint32_t lastWifiAttempt = 0;
  static uint32_t lastMqttAttempt = 0;
  static bool wifiWasConnected = false;
  const uint32_t now = millis();

  if (WiFi.status() != WL_CONNECTED) {
    if (wifiWasConnected) {
      Serial.println("Wi-Fi disconnected");
      wifiWasConnected = false;
    }

    if (now - lastWifiAttempt >= WIFI_RETRY_MS) {
      lastWifiAttempt = now;
      Serial.println("Retrying Wi-Fi connection...");
      WiFi.reconnect();
    }
    return;
  }

  if (!wifiWasConnected) {
    wifiWasConnected = true;
    lastMqttAttempt = now - MQTT_RETRY_MS;
    Serial.printf("Wi-Fi connected, IP: %s\n", WiFi.localIP().toString().c_str());
    if (!otaStarted) startOTA();
  }

  if (!mqtt.connected() && now - lastMqttAttempt >= MQTT_RETRY_MS) {
    lastMqttAttempt = now;
    Serial.print("MQTT connecting... ");
    if (mqtt.connect(mqttClientId, MQTT_USER, MQTT_PASS)) {
      Serial.println("OK");
      mqtt.subscribe(MQTT_TOPIC);
      stripDirty = true;
    } else {
      Serial.printf("failed, rc=%d\n", mqtt.state());
    }
  }
}

void setup() {
  Serial.begin(115200);
  FastLED.addLeds<WS2812B, DATA_PIN, GRB>(leds, NUM_LEDS);
  FastLED.clear(true);

  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.println("Connecting to Wi-Fi...");

  const uint64_t chipId = ESP.getEfuseMac();
  snprintf(mqttClientId, sizeof(mqttClientId), "ESP32_%04X%08X",
           static_cast<uint16_t>(chipId >> 32), static_cast<uint32_t>(chipId));
  snprintf(otaHostname, sizeof(otaHostname), "esp-light-%04x%08x",
           static_cast<uint16_t>(chipId >> 32), static_cast<uint32_t>(chipId));

  mqtt.setServer(MQTT_SERVER, MQTT_PORT);
  mqtt.setCallback(mqttCallback);
  mqtt.setSocketTimeout(NETWORK_TIMEOUT_MS / 1000);
}

void loop() {
  maintainConnections();
  if (otaStarted && WiFi.status() == WL_CONNECTED) ArduinoOTA.handle();
  if (mqtt.connected()) mqtt.loop();

  // one‐time OFF/STATIC updates
  if (stripDirty && currentMode != RAINBOW) {
    updateStrip();
    stripDirty = false;
  }

  // continuous RAINBOW animation at ~50 Hz
  static uint32_t lastMs = 0;
  if (currentMode == RAINBOW && millis() - lastMs >= 20) {
    lastMs = millis();
    updateRainbowFrame();
  }
}
