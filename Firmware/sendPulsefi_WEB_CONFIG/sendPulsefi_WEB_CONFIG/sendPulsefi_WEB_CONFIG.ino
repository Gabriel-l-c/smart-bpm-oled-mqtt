#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <Wire.h>
#include "MAX30105.h"
#include "heartRate.h"
#include <ESPmDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <WebServer.h>
#include <Preferences.h>

// --- CONFIGURAÇÕES GLOBAIS (Baseadas no seu Receiver) ---
const char* DEFAULT_WIFI_SSID = "ERUS 2.4GHz";
const char* DEFAULT_WIFI_PASS = "ultrabots3";
const char* DEFAULT_MQTT_SERVER = "192.168.0.117";
const int MQTT_PORT = 1883;
const char* MQTT_TOPIC_BPM = "health/bpm";
const char* MQTT_USER = "pulse";
const char* MQTT_PASS = "pulse10";

// Configurações OTA
const char* OTA_HOSTNAME = "ESP32-SENDER";
const char* OTA_PASSWORD = "pulse123";

// --- OBJETOS ---
WiFiClient espClient;
PubSubClient client(espClient);
MAX30105 particleSensor;
WebServer server(80);
Preferences preferences;

// --- VARIÁVEIS DE CONFIGURAÇÃO (Carregadas da Memória) ---
String wifi_ssid, wifi_password, mqtt_server;

// --- VARIÁVEIS DO SENSOR ---
const byte RATE_SIZE = 4;
byte rates[RATE_SIZE];
byte rateSpot = 0;
unsigned long lastBeat = 0;
float beatsPerMinute;
int beatAvg = 0;

// --- TIMERS ---
unsigned long lastMqttRetry = 0;
unsigned long lastMqttMsg = 0;

// ----------------------------------------------------------------------------------
// --- Funções de Configuração e Memória ---
// ----------------------------------------------------------------------------------

void loadConfig() {
  preferences.begin("config", false);
  wifi_ssid = preferences.getString("wifi_ssid", DEFAULT_WIFI_SSID);
  wifi_password = preferences.getString("wifi_pass", DEFAULT_WIFI_PASS);
  mqtt_server = preferences.getString("mqtt_server", DEFAULT_MQTT_SERVER);
  preferences.end();
}

void handleRoot() {
  String html = "<html><head><title>Sender Config</title></head><body>";
  html += "<h1>ESP32 Sender - Config</h1>";
  html += "<p>Status MQTT: " + String(client.connected() ? "Conectado" : "Desconectado") + "</p>";
  html += "<form action='/save' method='POST'>";
  html += "Broker IP: <input name='server' value='" + mqtt_server + "'><br>";
  html += "<button type='submit'>Salvar e Reiniciar</button></form></body></html>";
  server.send(200, "text/html", html);
}

void handleSave() {
  if (server.hasArg("server")) {
    preferences.begin("config", false);
    preferences.putString("mqtt_server", server.arg("server"));
    preferences.end();
    server.send(200, "text/html", "Salvo! Reiniciando...");
    delay(2000);
    ESP.restart();
  }
}

// ----------------------------------------------------------------------------------
// --- Funções de Rede (Padrão do seu Receiver) ---
// ----------------------------------------------------------------------------------

void setup_wifi() {
  Serial.print("\nConnecting to ");
  Serial.println(wifi_ssid);
  WiFi.mode(WIFI_STA);
  WiFi.begin(wifi_ssid.c_str(), wifi_password.c_str());

  int timeout = 0;
  while (WiFi.status() != WL_CONNECTED && timeout < 20) {
    delay(500);
    Serial.print(".");
    timeout++;
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi Connected. IP: " + WiFi.localIP().toString());
  }
}

void setup_ota() {
  ArduinoOTA.setHostname(OTA_HOSTNAME);
  ArduinoOTA.setPassword(OTA_PASSWORD);
  ArduinoOTA.onStart([]() { Serial.println("OTA Start"); });
  ArduinoOTA.onEnd([]() { Serial.println("\nOTA End"); });
  ArduinoOTA.onError([](ota_error_t error) { Serial.printf("Error[%u]\n", error); });
  ArduinoOTA.begin();
}

void reconnect() {
  if (millis() - lastMqttRetry > 5000) {
    lastMqttRetry = millis();
    Serial.print("Attempting MQTT connection...");
    String clientId = "ESP32_BPM_Sender_" + String(random(0xffff), HEX);
    
    if (client.connect(clientId.c_str(), MQTT_USER, MQTT_PASS)) {
      Serial.println("connected ✅");
    } else {
      Serial.print("failed, rc=");
      Serial.println(client.state());
    }
  }
}

// ----------------------------------------------------------------------------------
// --- Setup e Loop ---
// ----------------------------------------------------------------------------------

void setup() {
  Serial.begin(115200);
  loadConfig();
  
  Wire.begin(21, 22);
  
  // Inicializa Sensor
  if (!particleSensor.begin(Wire, I2C_SPEED_FAST)) {
    Serial.println("MAX30105 was not found. Check wiring/power.");
    while (1);
  }
  particleSensor.setup();
  particleSensor.setPulseAmplitudeRed(0x0A);
  particleSensor.setPulseAmplitudeGreen(0);

  setup_wifi();
  setup_ota();

  // Web Server
  server.on("/", handleRoot);
  server.on("/save", HTTP_POST, handleSave);
  server.begin();

  client.setServer(mqtt_server.c_str(), MQTT_PORT);
}

void loop() {
  ArduinoOTA.handle();
  server.handleClient();

  if (!client.connected()) {
    reconnect();
  } else {
    client.loop();
  }

  // Leitura do Sensor
  long irValue = particleSensor.getIR();

  if (checkForBeat(irValue) == true) {
    unsigned long delta = micros() - lastBeat;
    lastBeat = micros();
    beatsPerMinute = 60000000.0 / delta;

    if (beatsPerMinute < 255 && beatsPerMinute > 20) {
      rates[rateSpot++] = (byte)beatsPerMinute;
      rateSpot %= RATE_SIZE;
      beatAvg = 0;
      for (byte x = 0; x < RATE_SIZE; x++) beatAvg += rates[x];
      beatAvg /= RATE_SIZE;
    }
  }

  // Envio para o MQTT a cada 2 segundos se houver leitura
  if (millis() - lastMqttMsg > 2000) {
    lastMqttMsg = millis();
    if (beatAvg > 30 && client.connected()) {
      client.publish(MQTT_TOPIC_BPM, String(beatAvg).c_str());
      Serial.print("Sent BPM: ");
      Serial.println(beatAvg);
    }
  }
}