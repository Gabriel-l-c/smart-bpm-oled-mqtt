# 📡 Sistema OTA para Atualização de Credenciais WiFi
## Pulse Monitor - Documentação Completa

---

## 🎯 O QUE FOI ADICIONADO

Agora seus ESP32s podem ter **SSID, Senha WiFi e IP do MQTT Broker** atualizados **via Web (OTA)** sem precisar recompilar o código!

### ✨ Novos Recursos:

1. **Interface Web** para configuração
2. **Salvamento em memória EEPROM** (Preferences)
3. **mDNS** para acesso fácil via nome
4. **Credenciais padrão** como fallback
5. **Feedback visual** no OLED (apenas no Receiver)

---

## 📋 BIBLIOTECAS NECESSÁRIAS

Certifique-se de ter instaladas no Arduino IDE:

```
- Preferences (já vem com ESP32)
- WebServer (já vem com ESP32)
- ESPmDNS (já vem com ESP32)
- Wire
- MAX30105 (Sparkfun)
- heartRate (Sparkfun)
- WiFi
- PubSubClient
- Adafruit_GFX (apenas Receiver)
- Adafruit_SSD1306 (apenas Receiver)
```

---

## 🚀 COMO USAR

### **Primeira Vez (Upload do Código)**

1. **Abra o Arduino IDE**
2. **Carregue o código** (`sendPulsefi_OTA.ino` ou `recevePulsefi_OTA.ino`)
3. **Configure as credenciais padrão** (caso queira mudar os valores iniciais):
   ```cpp
   String WIFI_SSID = "ERUS 2.4GHz";           
   String WIFI_PASSWORD = "ultrabots3";     
   String MQTT_SERVER = "192.168.0.110";
   ```
4. **Faça o upload** para o ESP32
5. **Abra o Serial Monitor** (115200 baud)
6. **Anote o IP** que aparece após conectar ao WiFi

---

### **Atualizando Credenciais via OTA (Web)**

#### **Método 1: Por IP**

1. Conecte seu computador/celular à **mesma rede WiFi** do ESP32
2. Abra o navegador e digite o IP do ESP32:
   ```
   http://192.168.0.XXX
   ```
   (Substitua `XXX` pelo IP que apareceu no Serial Monitor)

#### **Método 2: Por Nome (mDNS)** ⭐ Recomendado

1. Conecte à mesma rede WiFi
2. Abra o navegador:
   - **Sender (sensor):** `http://pulse-sender.local`
   - **Receiver (OLED):** `http://pulse-receiver.local`

#### **Interface Web:**

Você verá uma página bonita com:
- ✅ Configurações atuais (SSID e MQTT Server)
- 📝 Formulário para alterar:
  - **SSID da Rede WiFi**
  - **Senha WiFi**
  - **IP do Broker MQTT**
- 💾 Botão "Salvar e Reiniciar"

#### **Salvando Novas Configurações:**

1. Preencha os campos com os novos valores
2. Clique em "Salvar e Reiniciar"
3. O ESP32 irá:
   - ✅ Salvar na memória EEPROM
   - 🔄 Reiniciar automaticamente
   - 📡 Conectar à nova rede WiFi

---

## 🔧 EXEMPLOS DE USO

### **Cenário 1: Mudou de roteador**

Você mudou seu roteador WiFi de "ERUS 2.4GHz" para "MinhaCasa_5G":

1. Conecte temporariamente à rede antiga (ERUS 2.4GHz)
2. Acesse: `http://pulse-sender.local`
3. Preencha:
   - SSID: `MinhaCasa_5G`
   - Senha: `novaSenha123`
   - MQTT Server: `192.168.0.110` (mantém o mesmo)
4. Salve → ESP32 reinicia → Conecta na nova rede!

### **Cenário 2: Mudou o IP do Broker MQTT**

Seu notebook (broker) mudou de IP `192.168.0.110` para `192.168.0.150`:

1. Acesse: `http://pulse-receiver.local`
2. Preencha:
   - SSID: `ERUS 2.4GHz` (mantém)
   - Senha: `ultrabots3` (mantém)
   - MQTT Server: `192.168.0.150` (novo IP)
3. Salve → Pronto!

### **Cenário 3: Levar para outro local**

Vai demonstrar o projeto na faculdade/empresa:

1. Conecte à rede WiFi local
2. Acesse via IP (veja no Serial Monitor)
3. Configure WiFi da faculdade
4. Configure IP do broker MQTT local
5. Tudo funciona sem recompilar!

---

## 🛡️ SEGURANÇA E FALLBACK

### **O que acontece se errar as credenciais?**

1. O ESP32 tenta conectar por 30 segundos
2. Se falhar, mantém as credenciais antigas na memória
3. No Serial Monitor aparece: "Failed to connect to WiFi. Check credentials."
4. Você pode:
   - **Opção A:** Conectar na rede antiga e corrigir
   - **Opção B:** Fazer upload do código novamente

### **Como resetar para padrão?**

Opção 1 - Via código:
```cpp
void setup() {
  preferences.begin("wifi-config", false);
  preferences.clear(); // Apaga tudo
  preferences.end();
  // ... resto do setup
}
```

Opção 2 - Via Serial Monitor (adicione esta função):
```cpp
void resetConfig() {
  preferences.begin("wifi-config", false);
  preferences.clear();
  preferences.end();
  Serial.println("Config reset! Restarting...");
  ESP.restart();
}
```

---

## 📊 MONITORAMENTO

### **Serial Monitor mostra:**

```
=== Configurações Carregadas ===
SSID: ERUS 2.4GHz
MQTT Server: 192.168.0.110

Connecting to ERUS 2.4GHz
........
WiFi connected
IP Address: 192.168.0.105
mDNS started: http://pulse-sender.local
HTTP server started on port 80
Access: http://192.168.0.105 or http://pulse-sender.local
```

### **OLED Display (apenas Receiver) mostra:**

Durante conexão:
```
Connecting to WiFi...
SSID: ERUS 2.4GHz
Attempt: 5/30
```

Após sucesso:
```
WiFi Connected!
IP: 192.168.0.106
```

---

## 🎨 DIFERENÇAS ENTRE SENDER E RECEIVER

### **Sender (Sensor MAX30105):**
- 🟢 Cor do botão: **Verde** (#4CAF50)
- 📛 Nome mDNS: `pulse-sender.local`
- 📡 Função: Publica BPM via MQTT

### **Receiver (Display OLED):**
- 🔵 Cor do botão: **Azul** (#2196F3)
- 📛 Nome mDNS: `pulse-receiver.local`
- 📺 Função: Recebe BPM e exibe
- 💓 Extra: Mostra BPM atual na página web

---

## 🐛 SOLUÇÃO DE PROBLEMAS

### **1. Não consigo acessar http://pulse-sender.local**

**Causa:** mDNS não funciona em algumas redes
**Solução:** Use o IP direto (`http://192.168.0.XXX`)

### **2. ESP32 não conecta após mudar credenciais**

**Causa:** SSID ou senha incorretos
**Solução:** 
- Reconecte à rede antiga
- Corrija as credenciais
- Ou faça upload do código novamente

### **3. "failed, rc=5" no MQTT**

**Causa:** Credenciais MQTT incorretas ou broker offline
**Solução:**
- Verifique se o Mosquitto está rodando
- Corrija MQTT_USER e MQTT_PASS se necessário

### **4. Como descobrir o IP do ESP32?**

**Métodos:**
1. Serial Monitor (melhor)
2. Roteador → Lista de dispositivos conectados
3. App "Fing" ou "Network Scanner" no celular
4. Comando: `ping pulse-sender.local` (se mDNS funcionar)

---

## 📱 ACESSO VIA CELULAR

1. Conecte o celular à mesma rede WiFi
2. Abra o navegador (Chrome/Safari)
3. Digite: `http://pulse-sender.local` ou o IP
4. Configure normalmente!

**Dica:** Salve nos favoritos para acesso rápido!

---

## 🔄 ATUALIZAÇÕES FUTURAS POSSÍVEIS

- [ ] AP Mode (WiFi próprio) se não conectar
- [ ] Autenticação web (usuário/senha)
- [ ] Configurar MQTT_USER e MQTT_PASS via web
- [ ] Backup/restore de configurações
- [ ] API REST para configuração programática

---

## 📞 SUPORTE

Se tiver dúvidas:
1. Verifique o Serial Monitor (115200 baud)
2. Teste com credenciais padrão primeiro
3. Confirme que está na mesma rede WiFi

---

## ✅ CHECKLIST DE INSTALAÇÃO

- [ ] Bibliotecas instaladas
- [ ] Código carregado no ESP32
- [ ] Serial Monitor aberto (115200 baud)
- [ ] IP anotado
- [ ] Testado acesso via navegador
- [ ] Configurações salvas com sucesso
- [ ] ESP32 reconectou à nova rede

---

**Pronto! Agora você tem controle total via OTA! 🎉**
