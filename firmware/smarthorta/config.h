// =====================================================================
//  config.h - Configuracoes do SmartHorta (pinos, rede, topicos, tempos)
//  Nada aqui e secreto. Credenciais ficam em secrets.h (fora do Git).
// =====================================================================
#pragma once

// ----------------- Identificacao -----------------
#define DISPOSITIVO_ID   "smarthorta-01"
#define FIRMWARE_VERSAO  "1.1"

// ----------------- Pinos (ESP32 DevKit C V4) -----------------
const int PINO_DHT       = 15;  // DHT22 - dados
const int PINO_SOLO      = 34;  // Sensor de umidade do solo (potenciometro no Wokwi) - ADC1
const int PINO_RELE      = 26;  // Modulo rele -> bomba d'agua
const int PINO_LED_VERDE = 18;  // Solo com umidade adequada
const int PINO_LED_VERM  = 19;  // Solo seco / alerta
const int PINO_LED_REDE  = 4;   // Amarelo: aceso = conectado ao broker, piscando = sem conexao
const int PINO_BUZZER    = 23;  // Alerta sonoro

// ----------------- Broker MQTT -----------------
// Broker publico HiveMQ, porta 8883 = MQTT sobre TLS (conexao criptografada)
#define MQTT_BROKER  "broker.hivemq.com"
#define MQTT_PORTA   8883

#define TOPICO_BASE   "smarthorta/kz-rps-4793"
#define TOPICO_DADOS  TOPICO_BASE "/dados"        // ESP32 publica (JSON)
#define TOPICO_STATUS TOPICO_BASE "/status"       // ESP32 publica "online"/"offline" (retido + Last Will)
#define TOPICO_BOMBA  TOPICO_BASE "/cmd/bomba"    // ESP32 recebe "ON" | "OFF" | "AUTO"
#define TOPICO_LIMITE TOPICO_BASE "/cmd/limite"   // ESP32 recebe inteiro de 10 a 90

// ----------------- Regras de irrigacao -----------------
const int LIMITE_PADRAO = 40;   // % de umidade do solo abaixo da qual irriga
const int LIMITE_MIN    = 10;
const int LIMITE_MAX    = 90;
const int HISTERESE     = 10;   // bomba desliga so quando passar de limite + histerese

// ----------------- Tempos (ms) -----------------
const unsigned long INTERVALO_LEITURA   = 2000;    // leitura + decisao + envio
const unsigned long INTERVALO_WIFI      = 10000;   // nova tentativa de Wi-Fi
const unsigned long INTERVALO_MQTT      = 5000;    // nova tentativa de broker
const unsigned long TEMPO_MAX_MANUAL    = 120000;  // modo manual expira e volta ao automatico (seguranca)
const unsigned long INTERVALO_MIN_CMD   = 1000;    // ignora comandos mais rapidos que isso (anti-flood)

// ----------------- Validacao de comandos -----------------
const unsigned int TAMANHO_MAX_CMD = 8;            // payloads maiores sao descartados
