//-------------------------------------------------------------
// FREQ + SENSOR + MICRO + NÃO MENOR QUE 58 BPM COM MQTT PUBLISHER + OTA
/*
  Monitor de Frequência Cardíaca (MAX30105) - Versão Não-Bloqueante com MQTT e OTA

  Objetivo:
  - Medição e cálculo do BPM (beatAvg).
  - Pulsação do LED controlada pelo BPM médio (beatAvg).
  - Envio do beatAvg via MQTT para outro dispositivo (Subscriber).
  - Atualização OTA (Over-The-Air) para facilitar updates remotos.
  
  Como usar o OTA:
  1. Após o primeiro upload via cabo USB, o ESP32 estará disponível para OTA
  2. No Arduino IDE, vá em: Ferramentas > Porta > Selecione o IP do ESP32 na rede
  3. Ou use: Tools > Port > Network Port (ESP32-SENDER at 192.168.x.x)
  4. Faça o upload normalmente - será feito via WiFi!
*/

// --- INCLUDES ---
#include <Wire.h>
#include "MAX30105.h"
#include "heartRate.h"
#include <WiFi.h>
#include <PubSubClient.h>
#include <ESPmDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>

// --- CONFIGURAÇÕES GLOBAIS ---
const char* WIFI_SSID = "ERUS 2.4GHz";           
const char* WIFI_PASSWORD = "ultrabots3";     
const char* MQTT_SERVER = "192.168.0.117"; // IP Local do Broker (Notebook)
const int MQTT_PORT = 1883;
const char* MQTT_TOPIC_BPM = "health/bpm";   
const char* MQTT_USER = "pulse";      
const char* MQTT_PASS = "pulse10"; 

// Configurações OTA
const char* OTA_HOSTNAME = "ESP32-SENDER";  // Nome que aparecerá na rede
const char* OTA_PASSWORD = "pulse123";      // Senha para proteger updates OTA

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
    } else { // U_SPIFFS
      type = "filesystem";
    }
    Serial.println("Start updating " + type);
  });
  
  ArduinoOTA.onEnd([]() {
    Serial.println("\nEnd");
  });
  
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  });
  
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) {
      Serial.println("Auth Failed");
    } else if (error == OTA_BEGIN_ERROR) {
      Serial.println("Begin Failed");
    } else if (error == OTA_CONNECT_ERROR) {
      Serial.println("Connect Failed");
    } else if (error == OTA_RECEIVE_ERROR) {
      Serial.println("Receive Failed");
    } else if (error == OTA_END_ERROR) {
      Serial.println("End Failed");
    }
  });

  ArduinoOTA.begin();
  Serial.println("OTA Ready");
  Serial.print("OTA Hostname: ");
  Serial.println(OTA_HOSTNAME);
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
}

void reconnect() {
  // Loop até estar conectado
  while (!client.connected()) {
    // 1. Verifica se o WiFi ainda está ativo
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
  Serial.println("Heart Rate Monitor (MQTT Sender) with OTA Initializing...");

  randomSeed((unsigned long)ESP.getEfuseMac());

  // I2C para Sensor
  Wire.begin(21, 22);
  
  // 1. Configuração WiFi
  setup_wifi();

  // 2. Configuração OTA (NOVO!)
  setup_ota();

  // 3. Configuração MQTT
  client.setServer(MQTT_SERVER, MQTT_PORT);

  // 4. Inicialização do Pino do LED
  pinMode(LED_PIN, OUTPUT);
  
  // 5. Inicialização do Sensor
  if (!particleSensor.begin(Wire, I2C_SPEED_FAST)) 
  {
    Serial.println("MAX30105 was not found. Please check wiring/power. ");
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
  // 0. Handle OTA (NOVO! - deve ser chamado constantemente)
  ArduinoOTA.handle();

  // 1. Manter a Conexão MQTT Ativa
  if (!client.connected()) {
    reconnect();
  }
  client.loop(); 

  // LEITURA DO SENSOR 
  long irValue = particleSensor.getIR();

  // *******************************************************
  // *** 2. CÁLCULO DO BPM ***
  // *******************************************************
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

  // *******************************************************
  // *** 3. CONTROLE DO PULSO (Ritmo e Fade) ***
  // *******************************************************
  
  // A. Ritmo do Pulso
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
  
  // B. Gerenciamento do Fade (Não-Bloqueante)
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

  // *******************************************************
  // *** 4. PUBLICAÇÃO MQTT (Temporizada) ***
  // *******************************************************
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
