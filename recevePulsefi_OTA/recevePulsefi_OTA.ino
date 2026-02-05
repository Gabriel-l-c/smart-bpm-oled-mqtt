//-------------------------------------------------------------
// DISPLAY OLED (0.96") COM SUBSCRIBER MQTT + OTA WiFi
/*
  Objetivo:
  - Conectar ao WiFi.
  - Assinar o tópico MQTT (health/bpm).
  - Exibir o BPM recebido no display OLED (SSD1306).
  - **NOVO**: Atualização OTA de SSID, Senha WiFi e IP do Broker MQTT
*/

// --- INCLUDES E BIBLIOTECAS ---
#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <driver/ledc.h>
#include <Preferences.h>
#include <WebServer.h>
#include <ESPmDNS.h>

// --- OBJETO PREFERENCES PARA SALVAR CREDENCIAIS ---
Preferences preferences;

// --- CONFIGURAÇÕES OLED ---
#define SCREEN_WIDTH 128      
#define SCREEN_HEIGHT 64      
#define OLED_RESET -1 
#define SCREEN_ADDRESS 0x3C

// --- CONFIGURAÇÕES PADRÃO (Fallback) ---
String WIFI_SSID = "ERUS 2.4GHz";           
String WIFI_PASSWORD = "ultrabots3";     
String MQTT_SERVER = "192.168.0.135";
const int MQTT_PORT = 1883;
const char* MQTT_TOPIC_BPM = "health/bpm";   
const char* MQTT_USER = "pulse";      
const char* MQTT_PASS = "pulse10"; 

// --- WEB SERVER PARA CONFIGURAÇÃO OTA ---
WebServer server(80);

// --- ADIÇÕES PARA PWM LED ---
const int LED_PIN = 14; 
const int LEDC_CHANNEL = 0;
const int LEDC_RESOLUTION = 10;
const int LEDC_FREQUENCY = 5000;

unsigned long lastPulseTime = 0;
int pulseIntervalMs = 1000;

// --- DECLARAÇÕES DE OBJETOS E VARIÁVEIS GLOBAIS ---
WiFiClient espClient;
PubSubClient client(espClient);
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

int receivedBPM = 0; 

// ----------------------------------------------------------------------------------
// --- Funções de Gerenciamento de Configurações ---
// ----------------------------------------------------------------------------------

void loadConfig() {
  preferences.begin("wifi-config", false);
  
  if (preferences.isKey("ssid")) {
    WIFI_SSID = preferences.getString("ssid", "ERUS 2.4GHz");
  }
  
  if (preferences.isKey("password")) {
    WIFI_PASSWORD = preferences.getString("password", "ultrabots3");
  }
  
  if (preferences.isKey("mqtt_server")) {
    MQTT_SERVER = preferences.getString("mqtt_server", "192.168.0.110");
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
  html += "<title>Configuração WiFi - Pulse Receiver</title>";
  html += "<style>";
  html += "body { font-family: Arial, sans-serif; max-width: 600px; margin: 50px auto; padding: 20px; background: #f0f0f0; }";
  html += "h1 { color: #333; text-align: center; }";
  html += ".card { background: white; padding: 30px; border-radius: 10px; box-shadow: 0 2px 10px rgba(0,0,0,0.1); }";
  html += "label { display: block; margin-top: 15px; font-weight: bold; color: #555; }";
  html += "input { width: 100%; padding: 10px; margin-top: 5px; border: 1px solid #ddd; border-radius: 5px; box-sizing: border-box; }";
  html += "button { width: 100%; padding: 12px; margin-top: 20px; background: #2196F3; color: white; border: none; border-radius: 5px; cursor: pointer; font-size: 16px; }";
  html += "button:hover { background: #0b7dda; }";
  html += ".info { background: #e8f5e9; padding: 15px; border-radius: 5px; margin-bottom: 20px; }";
  html += ".current { color: #1976d2; font-weight: bold; }";
  html += ".bpm-display { text-align: center; font-size: 48px; color: #f44336; margin: 20px 0; }";
  html += "</style>";
  html += "</head><body>";
  html += "<div class='card'>";
  html += "<h1>📡 Configuração WiFi OTA</h1>";
  
  // Mostra BPM atual se disponível
  if (receivedBPM > 0) {
    html += "<div class='bpm-display'>";
    html += "❤️ " + String(receivedBPM) + " BPM";
    html += "</div>";
  }
  
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
    
    saveConfig(newSSID, newPassword, newMQTTServer);
    
    String html = "<!DOCTYPE html><html><head>";
    html += "<meta charset='UTF-8'>";
    html += "<style>";
    html += "body { font-family: Arial, sans-serif; text-align: center; padding: 50px; background: #f0f0f0; }";
    html += ".success { background: white; padding: 30px; border-radius: 10px; max-width: 500px; margin: 0 auto; box-shadow: 0 2px 10px rgba(0,0,0,0.1); }";
    html += "h1 { color: #2196F3; }";
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
    
    // Feedback no OLED durante a conexão WiFi
    display.clearDisplay();
    display.setTextSize(1);
    display.setCursor(0,0);
    display.println("Connecting to WiFi...");
    display.print("SSID: ");
    display.println(WIFI_SSID);
    display.print("Attempt: ");
    display.print(attempts);
    display.print("/30");
    display.display();
    
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("");
    Serial.println("WiFi connected");
    Serial.print("IP Address: ");
    Serial.println(WiFi.localIP());
    
    // Inicia mDNS para acessar via http://pulse-receiver.local
    if (MDNS.begin("pulse-receiver")) {
      Serial.println("mDNS started: http://pulse-receiver.local");
    }
    
    // Feedback no OLED
    display.clearDisplay();
    display.setTextSize(1);
    display.setCursor(0,0);
    display.println("WiFi Connected!");
    display.print("IP: ");
    display.println(WiFi.localIP());
    display.display();
    delay(2000);
  } else {
    Serial.println("");
    Serial.println("Failed to connect to WiFi. Check credentials.");
    
    display.clearDisplay();
    display.setTextSize(1);
    display.setCursor(0,0);
    display.println("WiFi Failed!");
    display.println("Check credentials");
    display.println("Access:");
    display.println("http://192.168.4.1");
    display.display();
  }
}

void displayBPM() {
  display.clearDisplay();

  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println("Batimento Cardiaco");

  display.setTextSize(4);
  display.setCursor(0, 20);
  
  if (receivedBPM > 0) {
    display.print(receivedBPM);
    display.setTextSize(2);
    display.print(" BPM");
  } else {
    display.setTextSize(1);
    display.setCursor(0, 30);
    display.println("Waiting for data...");
    display.println("Check Sensor ESP32.");
  }
  
  display.display();
}

void updatePulseFrequency() {
  if (receivedBPM > 20) {
    pulseIntervalMs = (60 * 1000) / receivedBPM;
  } else {
    pulseIntervalMs = 3000;
  }
}

void ledPulse() {
  if (millis() - lastPulseTime >= pulseIntervalMs) {
    lastPulseTime = millis();
    
    int maxDuty = (1 << LEDC_RESOLUTION) - 1;

    // Fade IN
    for(int dutyCycle = 0; dutyCycle <= maxDuty; dutyCycle += 10) {
      ledcWrite(LEDC_CHANNEL, dutyCycle);
      delay(1);
    }

    delay(50); 
    
    // Fade OUT
    for(int dutyCycle = maxDuty; dutyCycle >= 0; dutyCycle -= 10) {
      ledc_set_duty(LEDC_LOW_SPEED_MODE, (ledc_channel_t)LEDC_CHANNEL, dutyCycle);
      ledc_update_duty(LEDC_LOW_SPEED_MODE, (ledc_channel_t)LEDC_CHANNEL);
      delay(2);
    }
    
    ledcWrite(LEDC_CHANNEL, 0);
  }
}

void callback(char* topic, byte* payload, unsigned int length) {
  Serial.print("Message arrived on topic: ");
  Serial.println(topic);

  String message;
  for (int i = 0; i < length; i++) {
    message += (char)payload[i];
  }
  Serial.print("Payload: ");
  Serial.println(message);

  int newBPM = message.toInt();
  if (newBPM > 0) {
    receivedBPM = newBPM;
    displayBPM();
    updatePulseFrequency(); 
  } else {
    Serial.println("Error converting received BPM.");
  }
}

void reconnect() {
  const char* fixedClientId = "ESP32_BPM_OLED_Receiver"; 
    
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
    
    if (client.connect(fixedClientId, MQTT_USER, MQTT_PASS)) {
      Serial.println("connected");
      client.subscribe(MQTT_TOPIC_BPM);
      Serial.print("Subscribed to: ");
      Serial.println(MQTT_TOPIC_BPM);
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

void setup() {
  Serial.begin(115200);
  
  randomSeed((unsigned long)ESP.getEfuseMac());
  
  // 1. Carrega configurações salvas
  loadConfig();
  
  // 2. I2C: Pinos GPIO 21 (SDA) e 22 (SCL)
  Wire.begin(21, 22);

  // 3. Inicialização do Display OLED (SSD1306)
  if(!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    Serial.println(F("SSD1306 allocation failed. Check SCL/SDA pins and 0x3C/0x3D address."));
    for(;;);
  }
  display.display(); 
  delay(2000); 
  display.clearDisplay();

  // 4. Configuração WiFi
  setup_wifi();

  // 5. Configuração Web Server OTA
  server.on("/", handleRoot);
  server.on("/save", HTTP_POST, handleSave);
  server.onNotFound(handleNotFound);
  server.begin();
  Serial.println("HTTP server started on port 80");
  Serial.print("Access: http://");
  Serial.print(WiFi.localIP());
  Serial.println(" or http://pulse-receiver.local");

  // 6. Configuração MQTT
  client.setServer(MQTT_SERVER.c_str(), MQTT_PORT);
  client.setCallback(callback);
  
  // 7. Configuração do LEDC (PWM)
  ledc_timer_config_t ledc_timer = {
    .speed_mode = LEDC_LOW_SPEED_MODE,
    .duty_resolution = (ledc_timer_bit_t)LEDC_RESOLUTION, 
    .timer_num = (ledc_timer_t)LEDC_CHANNEL,
    .freq_hz = LEDC_FREQUENCY,
    .clk_cfg = LEDC_AUTO_CLK,
  };
  ledc_timer_config(&ledc_timer);

  ledc_channel_config_t ledc_channel = {
    .gpio_num = LED_PIN,
    .speed_mode = LEDC_LOW_SPEED_MODE,
    .channel = (ledc_channel_t)LEDC_CHANNEL,
    .intr_type = LEDC_INTR_DISABLE,
    .timer_sel = (ledc_timer_t)LEDC_CHANNEL,
    .duty = 0,
    .hpoint = 0,
  };
  ledc_channel_config(&ledc_channel);
  
  // 8. Exibição inicial
  displayBPM();
}

// ----------------------------------------------------------------------------------
// --- Loop Principal ---
// ----------------------------------------------------------------------------------

void loop() {
  // 0. Processa requisições do Web Server
  server.handleClient();
  
  // 1. Manter a Conexão MQTT Ativa
  if (!client.connected()) {
    reconnect();
  }
  
  // 2. Processar mensagens MQTT
  client.loop(); 
  
  // 3. Pulsação do LED
  ledPulse(); 
}
