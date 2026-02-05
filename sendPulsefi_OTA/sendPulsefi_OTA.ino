//-------------------------------------------------------------
// FREQ + SENSOR + MICRO + NÃO MENOR QUE 58 BPM COM MQTT PUBLISHER + OTA WiFi
/*
  Monitor de Frequência Cardíaca (MAX30105) - Versão com OTA para WiFi

  Objetivo:
  - Medição e cálculo do BPM (beatAvg).
  - Pulsação do LED controlada pelo BPM médio (beatAvg).
  - Envio do beatAvg via MQTT para outro dispositivo (Subscriber).
  - **NOVO**: Atualização OTA de SSID, Senha WiFi e IP do Broker MQTT
*/

// --- INCLUDES ---
#include <Wire.h>
#include "MAX30105.h"
#include "heartRate.h"
#include <WiFi.h>
#include <PubSubClient.h>
#include <Preferences.h>
#include <WebServer.h>
#include <ESPmDNS.h>

// --- OBJETO PREFERENCES PARA SALVAR CREDENCIAIS ---
Preferences preferences;

// --- CONFIGURAÇÕES PADRÃO (Fallback) ---
String WIFI_SSID = "Gabriel";           
String WIFI_PASSWORD = "12345678";     
String MQTT_SERVER = "172.20.10.2"; 
const int MQTT_PORT = 1883;
const char* MQTT_TOPIC_BPM = "health/bpm";   
const char* MQTT_USER = "pulse";      
const char* MQTT_PASS = "pulse10"; 

// --- WEB SERVER PARA CONFIGURAÇÃO OTA ---
WebServer server(80);

// --- DECLARAÇÕES DE OBJETOS ---
WiFiClient espClient;
PubSubClient client(espClient);
MAX30105 particleSensor;

// --- VARIÁVEIS DO SENSOR ---
const byte RATE_SIZE = 4;
byte rates[RATE_SIZE];
byte rateSpot = 0;
unsigned long lastBeat = 0; 

float beatsPerMinute;
int beatAvg = 0; 

// --- VARIÁVEIS DE CONTROLE DO LED ---
const int LED_PIN = 14; 
long lastPulseTime = 0; 
int ledBrightness = 0; 
int fadeAmount = 5; 
unsigned long lastFadeTime = 0; 
const int PULSE_SPEED_US = 2000; 

// --- VARIÁVEIS DE CONTROLE MQTT ---
unsigned long lastMsg = 0;
const long MQTT_INTERVAL = 5000; 

// ----------------------------------------------------------------------------------
// --- Funções de Gerenciamento de Configurações ---
// ----------------------------------------------------------------------------------

void loadConfig() {
  preferences.begin("wifi-config", false);
  
  // Carrega SSID (se existir)
  if (preferences.isKey("ssid")) {
    WIFI_SSID = preferences.getString("ssid", "Gabriel");
  }
  
  // Carrega Password (se existir)
  if (preferences.isKey("password")) {
    WIFI_PASSWORD = preferences.getString("password", "12345678");
  }
  
  // Carrega MQTT Server (se existir)
  if (preferences.isKey("mqtt_server")) {
    MQTT_SERVER = preferences.getString("mqtt_server", "172.20.10.2");
  }
  
  preferences.end();
  
  Serial.println("=== Configurações Carregadas ===");
  Serial.print("SSID: ");
  Serial.println(WIFI_SSID);
  Serial.print("MQTT Server: ");
  Serial.println(MQTT_SERVER);
}

void saveConfig(String ssid, String password, String mqtt_server) {
  preferences.begin("wifi-config", false);
  
  preferences.putString("ssid", ssid);
  preferences.putString("password", password);
  preferences.putString("mqtt_server", mqtt_server);
  
  preferences.end();
  
  Serial.println("=== Configurações Salvas ===");
  Serial.print("SSID: ");
  Serial.println(ssid);
  Serial.print("MQTT Server: ");
  Serial.println(mqtt_server);
}

// ----------------------------------------------------------------------------------
// --- Páginas Web para Configuração ---
// ----------------------------------------------------------------------------------

void handleRoot() {
  String html = "<!DOCTYPE html><html><head>";
  html += "<meta charset='UTF-8'>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1.0'>";
  html += "<title>Configuração WiFi - Pulse Monitor</title>";
  html += "<style>";
  html += "body { font-family: Arial, sans-serif; max-width: 600px; margin: 50px auto; padding: 20px; background: #f0f0f0; }";
  html += "h1 { color: #333; text-align: center; }";
  html += ".card { background: white; padding: 30px; border-radius: 10px; box-shadow: 0 2px 10px rgba(0,0,0,0.1); }";
  html += "label { display: block; margin-top: 15px; font-weight: bold; color: #555; }";
  html += "input { width: 100%; padding: 10px; margin-top: 5px; border: 1px solid #ddd; border-radius: 5px; box-sizing: border-box; }";
  html += "button { width: 100%; padding: 12px; margin-top: 20px; background: #4CAF50; color: white; border: none; border-radius: 5px; cursor: pointer; font-size: 16px; }";
  html += "button:hover { background: #45a049; }";
  html += ".info { background: #e3f2fd; padding: 15px; border-radius: 5px; margin-bottom: 20px; }";
  html += ".current { color: #1976d2; font-weight: bold; }";
  html += "</style>";
  html += "</head><body>";
  html += "<div class='card'>";
  html += "<h1>⚙️ Configuração WiFi OTA</h1>";
  html += "<div class='info'>";
  html += "<p><strong>Configurações Atuais:</strong></p>";
  html += "<p>SSID: <span class='current'>" + WIFI_SSID + "</span></p>";
  html += "<p>MQTT Server: <span class='current'>" + MQTT_SERVER + "</span></p>";
  html += "</div>";
  html += "<form action='/save' method='POST'>";
  html += "<label>SSID da Rede WiFi:</label>";
  html += "<input type='text' name='ssid' value='" + WIFI_SSID + "' required>";
  html += "<label>Senha WiFi:</label>";
  html += "<input type='password' name='password' value='" + WIFI_PASSWORD + "' required>";
  html += "<label>IP do Broker MQTT:</label>";
  html += "<input type='text' name='mqtt_server' value='" + MQTT_SERVER + "' required>";
  html += "<button type='submit'>💾 Salvar e Reiniciar</button>";
  html += "</form>";
  html += "</div>";
  html += "</body></html>";
  
  server.send(200, "text/html", html);
}

void handleSave() {
  if (server.hasArg("ssid") && server.hasArg("password") && server.hasArg("mqtt_server")) {
    String newSSID = server.arg("ssid");
    String newPassword = server.arg("password");
    String newMQTTServer = server.arg("mqtt_server");
    
    // Salva as novas configurações
    saveConfig(newSSID, newPassword, newMQTTServer);
    
    String html = "<!DOCTYPE html><html><head>";
    html += "<meta charset='UTF-8'>";
    html += "<style>";
    html += "body { font-family: Arial, sans-serif; text-align: center; padding: 50px; background: #f0f0f0; }";
    html += ".success { background: white; padding: 30px; border-radius: 10px; max-width: 500px; margin: 0 auto; box-shadow: 0 2px 10px rgba(0,0,0,0.1); }";
    html += "h1 { color: #4CAF50; }";
    html += "</style>";
    html += "</head><body>";
    html += "<div class='success'>";
    html += "<h1>✅ Configurações Salvas!</h1>";
    html += "<p>O ESP32 irá reiniciar em 3 segundos...</p>";
    html += "<p>Conecte-se à nova rede WiFi para acessar novamente.</p>";
    html += "</div>";
    html += "</body></html>";
    
    server.send(200, "text/html", html);
    
    delay(3000);
    ESP.restart();
  } else {
    server.send(400, "text/plain", "Erro: Parâmetros inválidos");
  }
}

void handleNotFound() {
  server.send(404, "text/plain", "Página não encontrada");
}

// ----------------------------------------------------------------------------------
// --- Funções Auxiliares ---
// ----------------------------------------------------------------------------------

void setup_wifi() {
  delay(10);
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(WIFI_SSID);

  WiFi.begin(WIFI_SSID.c_str(), WIFI_PASSWORD.c_str());

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 30) {
    delay(500);
    Serial.print(".");
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("");
    Serial.println("WiFi connected");
    Serial.print("IP Address: ");
    Serial.println(WiFi.localIP());
    
    // Inicia mDNS para acessar via http://pulse-sender.local
    if (MDNS.begin("pulse-sender")) {
      Serial.println("mDNS started: http://pulse-sender.local");
    }
  } else {
    Serial.println("");
    Serial.println("Failed to connect to WiFi. Check credentials.");
  }
}

void reconnect() {
  while (!client.connected()) {
    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("WiFi lost, re-establishing...");
      setup_wifi(); 
      if (WiFi.status() != WL_CONNECTED) {
          delay(5000);
          continue; 
      }
    }
    
    Serial.print("Attempting MQTT connection...");
    String clientId = "ESP32Sender-";
    clientId += String(random(0xffff), HEX);
    
    if (client.connect(clientId.c_str(), MQTT_USER, MQTT_PASS)) {
      Serial.println("connected");
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" Retrying in 5 seconds");
      delay(5000);
    }
  }
}

// ----------------------------------------------------------------------------------
// --- Setup Principal ---
// ----------------------------------------------------------------------------------

void setup()
{
  Serial.begin(115200);
  Serial.println("Heart Rate Monitor (MQTT Sender) with OTA WiFi Config Initializing...");

  randomSeed((unsigned long)ESP.getEfuseMac());

  // 1. Carrega configurações salvas
  loadConfig();

  // 2. I2C para Sensor
  Wire.begin(21, 22);
  
  // 3. Configuração WiFi
  setup_wifi();

  // 4. Configuração Web Server OTA
  server.on("/", handleRoot);
  server.on("/save", HTTP_POST, handleSave);
  server.onNotFound(handleNotFound);
  server.begin();
  Serial.println("HTTP server started on port 80");
  Serial.print("Access: http://");
  Serial.print(WiFi.localIP());
  Serial.println(" or http://pulse-sender.local");

  // 5. Configuração MQTT
  client.setServer(MQTT_SERVER.c_str(), MQTT_PORT);

  // 6. Inicialização do Pino do LED
  pinMode(LED_PIN, OUTPUT);
  
  // 7. Inicialização do Sensor
  if (!particleSensor.begin(Wire, I2C_SPEED_FAST)) 
  {
    Serial.println("MAX30105 was not found. Please check wiring/power.");
    while (1);
  }
  Serial.println("Place your index finger on the sensor with steady pressure.");

  particleSensor.setup(); 
  particleSensor.setPulseAmplitudeRed(0x0A); 
  particleSensor.setPulseAmplitudeGreen(1); 
}

// ----------------------------------------------------------------------------------
// --- Loop Principal ---
// ----------------------------------------------------------------------------------

void loop()
{
  // 0. Processa requisições do Web Server
  server.handleClient();

  // 1. Manter a Conexão MQTT Ativa
  if (!client.connected()) {
    reconnect();
  }
  client.loop(); 

  // 2. LEITURA DO SENSOR 
  long irValue = particleSensor.getIR();

  // 3. CÁLCULO DO BPM
  if (checkForBeat(irValue) == true)
  {
    unsigned long delta = micros() - lastBeat;
    lastBeat = micros(); 
    
    if (delta > 0)  
    {
      beatsPerMinute = 60000000.0 / delta; 

      if (beatsPerMinute < 255 && beatsPerMinute > 20)
      {
        if (beatsPerMinute < 58) {
          beatsPerMinute = 60;
        }

        rates[rateSpot++] = (byte)beatsPerMinute;
        rateSpot %= RATE_SIZE;

        beatAvg = 0;
        for (byte x = 0 ; x < RATE_SIZE ; x++)
          beatAvg += rates[x];
        beatAvg /= RATE_SIZE;
      }
    }
  }

  // 4. CONTROLE DO PULSO (Ritmo e Fade)
  if (beatAvg > 20)  
  {
    long requiredInterval = 60000 / beatAvg; 

    if (millis() - lastPulseTime >= requiredInterval)
    {
        ledBrightness = 0;
        fadeAmount = 5;
        lastFadeTime = micros(); 
        lastPulseTime = millis(); 
    }
  }
  
  if (fadeAmount != 0 && (micros() - lastFadeTime >= PULSE_SPEED_US))
  {
      lastFadeTime = micros(); 

      ledBrightness = ledBrightness + fadeAmount; 

      if (ledBrightness >= 255) {
        ledBrightness = 255;
        fadeAmount = -5; 
      }
      
      if (ledBrightness <= 0) {
        ledBrightness = 0;
        fadeAmount = 0; 
      }

      analogWrite(LED_PIN, ledBrightness);
  }

  // 5. PUBLICAÇÃO MQTT (Temporizada)
  unsigned long now = millis();
  if (now - lastMsg > MQTT_INTERVAL) {
    lastMsg = now;

    if (beatAvg > 0) {
      String payload = String(beatAvg);
      
      if (client.publish(MQTT_TOPIC_BPM, payload.c_str())) {
        Serial.print("[MQTT] Published BPM: ");
        Serial.println(beatAvg);
      } else {
        Serial.print("[MQTT] Publish failed. State: ");
        Serial.println(client.state());
      }
    }
    
    Serial.print("IR=");
    Serial.print(irValue);
    Serial.print(", BPM=");
    Serial.print(beatsPerMinute);
    Serial.print(", Avg BPM=");
    Serial.println(beatAvg);
  }
  
  if (irValue < 50000)
    Serial.println("No finger? IR value is low.");
}
