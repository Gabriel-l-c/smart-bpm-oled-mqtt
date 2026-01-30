//-------------------------------------------------------------
// DISPLAY OLED (0.96") COM SUBSCRIBER MQTT
/*
  Objetivo:
  - Conectar ao WiFi.
  - Assinar o tópico MQTT (health/bpm).
  - Exibir o BPM recebido no display OLED (SSD1306).
*/

// --- INCLUDES E BIBLIOTECAS ---
#include <Arduino.h> // Garante que a API básica do Arduino/ESP32 seja carregada
#include <WiFi.h>
#include <PubSubClient.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <driver/ledc.h> // Header para as funções LEDC do ESP32

// --- CONFIGURAÇÕES OLED ---
#define SCREEN_WIDTH 128      
#define SCREEN_HEIGHT 64      
#define OLED_RESET -1 
#define SCREEN_ADDRESS 0x3C // Verifique se o endereço é 0x3C ou 0x3D para o seu módulo

// --- CONFIGURAÇÕES GLOBAIS ---
const char* WIFI_SSID = "ERUS 2.4GHz";           
const char* WIFI_PASSWORD = "ultrabots3";     
const char* MQTT_SERVER = "192.168.0.110"; // IP Local do Broker (Notebook)
const int MQTT_PORT = 1883;
const char* MQTT_TOPIC_BPM = "health/bpm";   

// *** DEFINIÇÕES DE AUTENTICAÇÃO (Se allow_anonymous for false) ***
// Se seu Mosquitto exigir usuário/senha, descomente e preencha:
const char* MQTT_USER = "pulse";      
const char* MQTT_PASS = "pulse10"; 

// --- ADIÇÕES PARA PWM LED ---
const int LED_PIN = 14; 
const int LEDC_CHANNEL = 0;   // Use um canal LEDC livre (0 a 15)
const int LEDC_RESOLUTION = 10; // 10 bits de resolução (0 a 1023)
const int LEDC_FREQUENCY = 5000; // Frequência do PWM (5kHz é um bom padrão)

unsigned long lastPulseTime = 0; // Para controle de tempo do ciclo de pulsação
int pulseIntervalMs = 1000; // Intervalo inicial em ms (equivalente a 60 BPM)
// --- FIM ADIÇÕES PARA PWM LED ---

// --- DECLARAÇÕES DE OBJETOS E VARIÁVEIS GLOBAIS ---
WiFiClient espClient;
PubSubClient client(espClient);
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// Variável para armazenar o último BPM recebido
int receivedBPM = 0; 

// ----------------------------------------------------------------------------------
// --- Funções Auxiliares ---
// ----------------------------------------------------------------------------------

void setup_wifi() {
  delay(10);
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(WIFI_SSID);

  // ... (Código de setup_wifi inalterado) ...
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

// Função para exibir o BPM no display OLED
void displayBPM() {
  // ... (Código de displayBPM inalterado) ...
  display.clearDisplay();

  // Título
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println("Batimento Cardiaco");

  // Valor do BPM
  display.setTextSize(4); // Tamanho grande para o valor principal
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
  
  display.display(); // Atualiza o display com o novo conteúdo
}

// --- ADIÇÃO: FUNÇÃO PARA CONTROLAR A PULSAÇÃO E FREQUÊNCIA DO LED ---
void updatePulseFrequency() {
  if (receivedBPM > 20) { // Garante um valor mínimo razoável
    // Calcula o intervalo de tempo entre pulsos (em milissegundos)
    // Formula: (60 segundos/minuto * 1000 ms/segundo) / BPM
    pulseIntervalMs = (60 * 1000) / receivedBPM;
  } else {
    // Valor padrão ou muito lento se o BPM for muito baixo ou 0
    pulseIntervalMs = 3000; // Exemplo: 20 BPM
  }
}

void ledPulse() {
  // A pulsação deve ocorrer na frequência do BPM
  if (millis() - lastPulseTime >= pulseIntervalMs) {
    // Reinicia o tempo para o próximo pulso
    lastPulseTime = millis();
    
    // Inicia a pulsação/brilho
    // A animação de pulso (fade in/out) é executada rapidamente, 
    // mas o *intervalo* entre os pulsos é controlado pelo BPM.
    
    int maxDuty = (1 << LEDC_RESOLUTION) - 1; // 1023 para 10 bits

    // Fade IN (Brilha)
    for(int dutyCycle = 0; dutyCycle <= maxDuty; dutyCycle += 10) {
      ledcWrite(LEDC_CHANNEL, dutyCycle);
      delay(1); // Ajuste para a velocidade de fade
    }

    // Mantém no máximo brilho por um instante
    delay(50); 
    
    // Fade OUT (Apaga)
    for(int dutyCycle = maxDuty; dutyCycle >= 0; dutyCycle -= 10) {
      ledc_set_duty(LEDC_LOW_SPEED_MODE, (ledc_channel_t)LEDC_CHANNEL, dutyCycle);
ledc_update_duty(LEDC_LOW_SPEED_MODE, (ledc_channel_t)LEDC_CHANNEL);
      delay(2); // Ajuste para a velocidade de fade
    }
    
    ledcWrite(LEDC_CHANNEL, 0); // Garante que esteja completamente apagado no final
    
    // O restante do tempo *pulseIntervalMs* é gasto esperando o próximo pulso.
    // O delay(1) e delay(2) acima somam cerca de 300ms a 400ms do tempo total.
  }
}
// --- FIM ADIÇÃO: FUNÇÃO PARA CONTROLAR A PULSAÇÃO E FREQUÊNCIA DO LED ---


// Função de Callback para processar mensagens MQTT recebidas
void callback(char* topic, byte* payload, unsigned int length) {
  Serial.print("Message arrived on topic: ");
  Serial.println(topic);

  // Converte o payload (bytes) para String
  String message;
  for (int i = 0; i < length; i++) {
    message += (char)payload[i];
  }
  Serial.print("Payload: ");
  Serial.println(message);

  // Tenta converter a string do BPM para int
  int newBPM = message.toInt();
  if (newBPM > 0) {
    receivedBPM = newBPM;
    displayBPM(); // Atualiza o display imediatamente
    
    // --- ADIÇÃO: Atualiza a frequência de pulso ao receber um novo BPM ---
    updatePulseFrequency(); 
    // --- FIM ADIÇÃO ---
  } else {
    Serial.println("Error converting received BPM.");
  }
}

void reconnect() {
  // ... (Código de reconnect inalterado) ...
  // Use um ID de Cliente FIXO e único para a rede
  const char* fixedClientId = "ESP32_BPM_OLED_Receiver"; 
    
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
    
    // Tenta se conectar. Se você estiver usando autenticação, use:
    // client.connect(fixedClientId, MQTT_USER, MQTT_PASS)
    if (client.connect(fixedClientId,MQTT_USER, MQTT_PASS)) { // Usa ID fixo (funciona com allow_anonymous true)
      Serial.println("connected");
      // Uma vez conectado, assina o tópico
      client.subscribe(MQTT_TOPIC_BPM);
      Serial.print("Subscribed to: ");
      Serial.println(MQTT_TOPIC_BPM);
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state()); // Aqui é onde aparecia o rc=5
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
    for(;;); // Não continua se o display falhar
  }
  display.display(); 
  delay(2000); 
  display.clearDisplay();

  // 2. Configuração WiFi (necessária antes do MQTT)
  setup_wifi();

  // 3. Configuração MQTT
  client.setServer(MQTT_SERVER, MQTT_PORT);
  client.setCallback(callback); // Define a função que lida com as mensagens recebidas
  
// --- ADIÇÃO: Configuração do LEDC (PWM) usando ESP-IDF ---
  // 1. Configurar o Timer
  ledc_timer_config_t ledc_timer = {
    .speed_mode = LEDC_LOW_SPEED_MODE, // Modo de baixa velocidade para a maioria dos GPIOs
    .duty_resolution = (ledc_timer_bit_t)LEDC_RESOLUTION, 
    .timer_num = (ledc_timer_t)LEDC_CHANNEL, // Usa o número do canal como timer
    .freq_hz = LEDC_FREQUENCY,
    .clk_cfg = LEDC_AUTO_CLK,
  };
  ledc_timer_config(&ledc_timer);

  // 2. Configurar o Canal
  ledc_channel_config_t ledc_channel = {
    .gpio_num = LED_PIN,
    .speed_mode = LEDC_LOW_SPEED_MODE,
    .channel = (ledc_channel_t)LEDC_CHANNEL,
    .intr_type = LEDC_INTR_DISABLE,
    .timer_sel = (ledc_timer_t)LEDC_CHANNEL, // Liga ao timer configurado acima
    .duty = 0, // Começa desligado
    .hpoint = 0,
  };
  ledc_channel_config(&ledc_channel);
// --- FIM ADIÇÃO ---
  // 4. Exibição inicial
  displayBPM();
}

// ----------------------------------------------------------------------------------
// --- Loop Principal ---
// ----------------------------------------------------------------------------------

void loop() {
  // 1. Manter a Conexão MQTT Ativa
  if (!client.connected()) {
    reconnect();
  }
  
  // 2. Processar mensagens MQTT
  client.loop(); 
  
  // --- ADIÇÃO: Chama a função de pulsação do LED a cada ciclo ---
  // Esta função é não-bloqueante (usa millis() internamente para a frequência)
  ledPulse(); 
  // --- FIM ADIÇÃO ---
}