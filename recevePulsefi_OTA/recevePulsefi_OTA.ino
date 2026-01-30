//-------------------------------------------------------------
// DISPLAY OLED (0.96") COM SUBSCRIBER MQTT + OTA
/*
  Objetivo:
  - Conectar ao WiFi.
  - Assinar o tópico MQTT (health/bpm).
  - Exibir o BPM recebido no display OLED (SSD1306).
  - Atualização OTA (Over-The-Air) para facilitar updates remotos.
  
  Como usar o OTA:
  1. Após o primeiro upload via cabo USB, o ESP32 estará disponível para OTA
  2. No Arduino IDE, vá em: Ferramentas > Porta > Selecione o IP do ESP32 na rede
  3. Ou use: Tools > Port > Network Port (ESP32-RECEIVER at 192.168.x.x)
  4. Faça o upload normalmente - será feito via WiFi!
*/

// --- INCLUDES E BIBLIOTECAS ---
#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <driver/ledc.h>
#include <ESPmDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>

// --- CONFIGURAÇÕES OLED ---
#define SCREEN_WIDTH 128      
#define SCREEN_HEIGHT 64      
#define OLED_RESET -1 
#define SCREEN_ADDRESS 0x3C

// --- CONFIGURAÇÕES GLOBAIS ---
const char* WIFI_SSID = "ERUS 2.4GHz";           
const char* WIFI_PASSWORD = "ultrabots3";     
const char* MQTT_SERVER = "192.168.0.117";
const int MQTT_PORT = 1883;
const char* MQTT_TOPIC_BPM = "health/bpm";   
const char* MQTT_USER = "pulse";      
const char* MQTT_PASS = "pulse10"; 

// Configurações OTA
const char* OTA_HOSTNAME = "ESP32-RECEIVER";  // Nome que aparecerá na rede
const char* OTA_PASSWORD = "pulse123";         // Senha para proteger updates OTA

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
// --- Funções Auxiliares ---
// ----------------------------------------------------------------------------------

void setup_wifi() {
  delay(10);
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(WIFI_SSID);

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
    // Feedback no OLED durante a conexão WiFi
    display.clearDisplay();
    display.setTextSize(1);
    display.setCursor(0,0);
    display.println("Connecting to WiFi...");
    display.display();
  }

  Serial.println("");
  Serial.println("WiFi connected");
  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());
}

void setup_ota() {
  // Configurações do ArduinoOTA
  ArduinoOTA.setHostname(OTA_HOSTNAME);
  ArduinoOTA.setPassword(OTA_PASSWORD);

  // Callbacks para monitorar o processo OTA
  ArduinoOTA.onStart([]() {
    String type;
    if (ArduinoOTA.getCommand() == U_FLASH) {
      type = "sketch";
    } else {
      type = "filesystem";
    }
    Serial.println("Start updating " + type);
    
    // Mostrar no display que está atualizando
    display.clearDisplay();
    display.setTextSize(1);
    display.setCursor(0,0);
    display.println("OTA Update");
    display.println("Starting...");
    display.display();
  });
  
  ArduinoOTA.onEnd([]() {
    Serial.println("\nEnd");
    display.clearDisplay();
    display.setTextSize(1);
    display.setCursor(0,0);
    display.println("OTA Update");
    display.println("Complete!");
    display.println("Rebooting...");
    display.display();
  });
  
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    unsigned int percent = (progress / (total / 100));
    Serial.printf("Progress: %u%%\r", percent);
    
    // Atualizar display a cada 10%
    if (percent % 10 == 0) {
      display.clearDisplay();
      display.setTextSize(1);
      display.setCursor(0,0);
      display.println("OTA Update");
      display.setTextSize(2);
      display.setCursor(0,20);
      display.print(percent);
      display.println("%");
      display.display();
    }
  });
  
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    display.clearDisplay();
    display.setTextSize(1);
    display.setCursor(0,0);
    display.println("OTA Error!");
    
    if (error == OTA_AUTH_ERROR) {
      Serial.println("Auth Failed");
      display.println("Auth Failed");
    } else if (error == OTA_BEGIN_ERROR) {
      Serial.println("Begin Failed");
      display.println("Begin Failed");
    } else if (error == OTA_CONNECT_ERROR) {
      Serial.println("Connect Failed");
      display.println("Connect Failed");
    } else if (error == OTA_RECEIVE_ERROR) {
      Serial.println("Receive Failed");
      display.println("Receive Failed");
    } else if (error == OTA_END_ERROR) {
      Serial.println("End Failed");
      display.println("End Failed");
    }
    display.display();
    delay(3000);
  });

  ArduinoOTA.begin();
  Serial.println("OTA Ready");
  Serial.print("OTA Hostname: ");
  Serial.println(OTA_HOSTNAME);
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
}

void displayBPM() {
  display.clearDisplay();

  // Título
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println("Batimento Cardiaco");

  // Valor do BPM
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
  
  // I2C: Pinos GPIO 21 (SDA) e 22 (SCL)
  Wire.begin(21, 22);

  // 1. Inicialização do Display OLED (SSD1306)
  if(!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    Serial.println(F("SSD1306 allocation failed. Check SCL/SDA pins and 0x3C/0x3D address."));
    for(;;);
  }
  display.display(); 
  delay(2000); 
  display.clearDisplay();

  // 2. Configuração WiFi
  setup_wifi();

  // 3. Configuração OTA (NOVO!)
  setup_ota();

  // 4. Configuração MQTT
  client.setServer(MQTT_SERVER, MQTT_PORT);
  client.setCallback(callback);
  
  // 5. Configuração do LEDC (PWM)
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

  // 6. Exibição inicial
  displayBPM();
}

// ----------------------------------------------------------------------------------
// --- Loop Principal ---
// ----------------------------------------------------------------------------------

void loop() {
  // 0. Handle OTA (NOVO! - deve ser chamado constantemente)
  ArduinoOTA.handle();

  // 1. Manter a Conexão MQTT Ativa
  if (!client.connected()) {
    reconnect();
  }
  
  // 2. Processar mensagens MQTT
  client.loop(); 
  
  // 3. Pulsação do LED
  ledPulse(); 
}
