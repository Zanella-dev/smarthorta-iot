// =====================================================================
//  secrets.example.h - MODELO de credenciais
//
//  1. Copie este arquivo para "secrets.h" (mesma pasta).
//  2. Preencha com os dados da sua rede/broker.
//  3. O arquivo secrets.h esta no .gitignore e NUNCA deve ir para o GitHub.
//
//  No Wokwi use a rede "Wokwi-GUEST" com senha vazia.
// =====================================================================
#pragma once

#define WIFI_SSID     "NOME_DA_SUA_REDE"
#define WIFI_SENHA    "SENHA_DA_SUA_REDE"

// Broker publico HiveMQ nao exige usuario/senha (deixe vazio).
// Para um broker privado (ex.: HiveMQ Cloud), preencha aqui.
#define MQTT_USUARIO  ""
#define MQTT_SENHA    ""
