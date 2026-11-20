/*
  =====================================================================
  SmartHorta - Monitoramento e Irrigacao Inteligente para Hortas Urbanas
  Integrantes: Kaua Zanela e Rafael Pereira da Silva
  Disciplina: Internet das Coisas - 2o semestre/2026
  =====================================================================

  Camadas IoT:
    Percepcao     -> DHT22 (temperatura/umidade do ar) + sensor de umidade do solo
                     (no Wokwi, um potenciometro simula o sensor)
    Processamento -> o ESP32 decide localmente quando ligar a bomba e aciona LEDs/buzzer.
                     A decisao NAO depende da internet (requisito R2).
    Rede          -> Wi-Fi + MQTT sobre TLS (porta 8883) no broker publico HiveMQ
    Aplicacao     -> Dashboard web (dashboard/index.html) + LEDs no proprio circuito

  Organizacao do loop (nada bloqueia):
    1. gerenciarWiFi()   - reconecta o Wi-Fi em segundo plano
    2. gerenciarMQTT()   - reconecta o broker em segundo plano
    3. mqtt.loop()       - recebe comandos
    4. a cada 2 s: lerSensores() -> decidirIrrigacao() -> atualizarSinalizacao() -> publicarDados()
*/

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include "DHTesp.h"

#include "config.h"        // pinos, topicos, tempos (publico)
#include "secrets.h"       // credenciais (fora do Git - ver secrets.example.h)
#include "certificados.h"  // CA raiz para validar o broker via TLS

// ----------------- Objetos -----------------
DHTesp dht;
WiFiClientSecure clienteTLS;
PubSubClient mqtt(clienteTLS);

// ----------------- Estado do sistema -----------------
struct Estado {
  float temperatura   = NAN;
  float umidadeAr     = NAN;
  int   umidadeSolo   = 0;
  bool  sensorArOk    = false;
  bool  bombaLigada   = false;
  bool  modoAuto      = true;
  int   limiteSolo    = LIMITE_PADRAO;
  bool  soloSecoAntes = false;
} estado;

unsigned long ultimaLeitura      = 0;
unsigned long ultimaTentativaWiFi = 0;
unsigned long ultimaTentativaMQTT = 0;
unsigned long inicioModoManual   = 0;
unsigned long ultimoComando      = 0;
bool wifiAntes = false;

// =====================================================================
//  ATUADORES
// =====================================================================
void ligarBomba(bool ligar) {
  if (ligar == estado.bombaLigada) return;
  estado.bombaLigada = ligar;
  digitalWrite(PINO_RELE, ligar ? HIGH : LOW);
  Serial.println(ligar ? ">> Bomba LIGADA" : ">> Bomba DESLIGADA");
}

// Bipes curtos (bloqueia no maximo ~600 ms, so quando o solo acabou de secar)
void bipar(int vezes) {
  for (int i = 0; i < vezes; i++) {
    tone(PINO_BUZZER, 1000);
    delay(120);
    noTone(PINO_BUZZER);
    delay(80);
  }
}

// =====================================================================
//  PERCEPCAO
// =====================================================================
void lerSensores() {
  TempAndHumidity leitura = dht.getTempAndHumidity();
  estado.sensorArOk = !isnan(leitura.temperature) && !isnan(leitura.humidity);
  if (estado.sensorArOk) {
    estado.temperatura = leitura.temperature;
    estado.umidadeAr   = leitura.humidity;
  } else {
    Serial.println("[ERRO] Falha na leitura do DHT22 - mantendo ultimo valor valido");
  }
  // ADC de 12 bits (0-4095) convertido para 0-100 %
  estado.umidadeSolo = constrain(map(analogRead(PINO_SOLO), 0, 4095, 0, 100), 0, 100);
}

// =====================================================================
//  PROCESSAMENTO (decisao local - funciona sem internet)
// =====================================================================
void decidirIrrigacao() {
  // Seguranca: o modo manual expira sozinho (evita bomba ligada indefinidamente)
  if (!estado.modoAuto && millis() - inicioModoManual > TEMPO_MAX_MANUAL) {
    estado.modoAuto = true;
    Serial.println("[SEGURANCA] Modo manual expirou - voltando ao automatico");
  }

  if (estado.modoAuto) {
    if (estado.umidadeSolo < estado.limiteSolo) {
      ligarBomba(true);                                        // solo seco: irriga
    } else if (estado.umidadeSolo >= estado.limiteSolo + HISTERESE) {
      ligarBomba(false);                                       // solo umido: para
    }                                                          // entre os dois: mantem (histerese)
  }
}

void atualizarSinalizacao() {
  bool soloSeco = estado.umidadeSolo < estado.limiteSolo;
  digitalWrite(PINO_LED_VERM,  soloSeco ? HIGH : LOW);
  digitalWrite(PINO_LED_VERDE, soloSeco ? LOW  : HIGH);
  if (soloSeco && !estado.soloSecoAntes) bipar(3);  // alerta sonoro na transicao para seco
  estado.soloSecoAntes = soloSeco;
}

void atualizarLedRede() {
  if (mqtt.connected()) {
    digitalWrite(PINO_LED_REDE, HIGH);                          // conectado: aceso
  } else {
    digitalWrite(PINO_LED_REDE, (millis() / 500) % 2);          // sem conexao: piscando
  }
}

// =====================================================================
//  REDE - Wi-Fi e MQTT nao bloqueantes
// =====================================================================
void gerenciarWiFi() {
  bool conectado = WiFi.status() == WL_CONNECTED;
  if (conectado != wifiAntes) {
    wifiAntes = conectado;
    if (conectado) {
      Serial.print("[REDE] Wi-Fi conectado. IP: ");
      Serial.println(WiFi.localIP());
    } else {
      Serial.println("[REDE] Wi-Fi desconectado - sistema segue em modo local");
    }
  }
  if (!conectado && millis() - ultimaTentativaWiFi > INTERVALO_WIFI) {
    ultimaTentativaWiFi = millis();
    Serial.println("[REDE] Tentando conectar ao Wi-Fi...");
    WiFi.disconnect();
    WiFi.begin(WIFI_SSID, WIFI_SENHA, 6);
  }
}

void gerenciarMQTT() {
  if (WiFi.status() != WL_CONNECTED || mqtt.connected()) return;
  if (millis() - ultimaTentativaMQTT < INTERVALO_MQTT) return;
  ultimaTentativaMQTT = millis();

  String id = String(DISPOSITIVO_ID) + "-" + String((uint32_t)ESP.getEfuseMac(), HEX);
  const char* usuario = strlen(MQTT_USUARIO) > 0 ? MQTT_USUARIO : nullptr;
  const char* senha   = strlen(MQTT_SENHA)   > 0 ? MQTT_SENHA   : nullptr;

  Serial.print("[REDE] Conectando ao broker (TLS)... ");
  // Last Will: se o ESP32 cair, o broker publica "offline" no lugar dele
  if (mqtt.connect(id.c_str(), usuario, senha, TOPICO_STATUS, 0, true, "offline")) {
    Serial.println("conectado!");
    mqtt.publish(TOPICO_STATUS, "online", true);
    mqtt.subscribe(TOPICO_BOMBA);
    mqtt.subscribe(TOPICO_LIMITE);
  } else {
    Serial.print("falhou (rc=");
    Serial.print(mqtt.state());
    Serial.println(") - nova tentativa em 5 s");
    char erroTLS[100];
    if (clienteTLS.lastError(erroTLS, sizeof(erroTLS)) != 0) {
      Serial.print("[REDE] Detalhe do TLS: ");
      Serial.println(erroTLS);
    }
  }
}

// =====================================================================
//  APLICACAO - comandos recebidos (com validacao de entrada)
// =====================================================================
void aoReceberComando(char* topico, byte* payload, unsigned int tamanho) {
  // 1) Tamanho: descarta mensagens grandes demais
  if (tamanho == 0 || tamanho > TAMANHO_MAX_CMD) {
    Serial.println("[CMD] Rejeitado: tamanho invalido");
    return;
  }
  // 2) Taxa: ignora comandos em rajada
  if (millis() - ultimoComando < INTERVALO_MIN_CMD) {
    Serial.println("[CMD] Rejeitado: comandos muito rapidos");
    return;
  }
  ultimoComando = millis();

  char msg[TAMANHO_MAX_CMD + 1];
  memcpy(msg, payload, tamanho);
  msg[tamanho] = '\0';
  String cmd = String(msg);
  cmd.trim();
  cmd.toUpperCase();

  Serial.print("[CMD] ");
  Serial.print(topico);
  Serial.print(" = ");
  Serial.println(cmd);

  // 3) Lista branca de comandos
  if (strcmp(topico, TOPICO_BOMBA) == 0) {
    if (cmd == "ON" || cmd == "OFF") {
      estado.modoAuto = false;
      inicioModoManual = millis();
      ligarBomba(cmd == "ON");
    } else if (cmd == "AUTO") {
      estado.modoAuto = true;
    } else {
      Serial.println("[CMD] Rejeitado: comando de bomba desconhecido");
      return;
    }
  } else if (strcmp(topico, TOPICO_LIMITE) == 0) {
    // 4) Numero inteiro dentro da faixa permitida
    for (unsigned int i = 0; i < cmd.length(); i++) {
      if (!isDigit(cmd[i])) {
        Serial.println("[CMD] Rejeitado: limite nao numerico");
        return;
      }
    }
    int novo = cmd.toInt();
    if (novo < LIMITE_MIN || novo > LIMITE_MAX) {
      Serial.println("[CMD] Rejeitado: limite fora da faixa 10-90");
      return;
    }
    estado.limiteSolo = novo;
  }
  ultimaLeitura = 0;  // forca nova leitura/publicacao para o dashboard responder rapido
}

void publicarDados() {
  char json[256];
  char temp[8], umid[8];
  if (estado.sensorArOk || !isnan(estado.temperatura)) {
    dtostrf(estado.temperatura, 1, 1, temp);
    dtostrf(estado.umidadeAr, 1, 1, umid);
  } else {
    strcpy(temp, "null");
    strcpy(umid, "null");
  }
  snprintf(json, sizeof(json),
           "{\"id\":\"%s\",\"solo\":%d,\"temp\":%s,\"umid\":%s,\"bomba\":%s,"
           "\"modo\":\"%s\",\"limite\":%d,\"sensor_ok\":%s,\"uptime\":%lu}",
           DISPOSITIVO_ID, estado.umidadeSolo, temp, umid,
           estado.bombaLigada ? "true" : "false",
           estado.modoAuto ? "auto" : "manual",
           estado.limiteSolo,
           estado.sensorArOk ? "true" : "false",
           millis() / 1000);

  Serial.print("[DADOS] ");
  Serial.println(json);

  if (mqtt.connected()) {
    if (!mqtt.publish(TOPICO_DADOS, json)) Serial.println("[ERRO] Falha ao publicar");
  }
}

// =====================================================================
//  SETUP e LOOP
// =====================================================================
void setup() {
  Serial.begin(115200);
  Serial.println();
  Serial.println("=== SmartHorta v" FIRMWARE_VERSAO " iniciando ===");

  pinMode(PINO_RELE, OUTPUT);
  pinMode(PINO_LED_VERDE, OUTPUT);
  pinMode(PINO_LED_VERM, OUTPUT);
  pinMode(PINO_LED_REDE, OUTPUT);
  pinMode(PINO_BUZZER, OUTPUT);
  digitalWrite(PINO_RELE, LOW);   // estado seguro: bomba desligada

  dht.setup(PINO_DHT, DHTesp::DHT22);

  clienteTLS.setCACert(CA_RAIZ);  // valida o certificado do broker
  mqtt.setServer(MQTT_BROKER, MQTT_PORTA);
  mqtt.setCallback(aoReceberComando);
  mqtt.setKeepAlive(90);          // tolera simulacao lenta no Wokwi
  mqtt.setSocketTimeout(5);       // nao trava o loop se o broker nao responder

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_SENHA, 6);  // nao espera: o controle local ja comeca a rodar
  ultimaTentativaWiFi = millis();

  bipar(1);
}

void loop() {
  gerenciarWiFi();
  gerenciarMQTT();
  mqtt.loop();
  atualizarLedRede();

  if (millis() - ultimaLeitura < INTERVALO_LEITURA) return;
  ultimaLeitura = millis();

  lerSensores();           // 1) Percepcao
  decidirIrrigacao();      // 2) Processamento
  atualizarSinalizacao();  //    sinalizacao local (LEDs + buzzer)
  publicarDados();         // 3) Rede -> 4) Aplicacao
}
