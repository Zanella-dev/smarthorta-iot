# 🌱 SmartHorta — Monitoramento e Irrigação Inteligente

Trabalho final da disciplina de **Internet das Coisas** (2º semestre/2026, Prof. Marcos Augusto Campagnaro Mucelini).

**Integrantes:** Kauã Zanela e Rafael Pereira da Silva

O SmartHorta mede a umidade do solo e as condições do ar de uma horta, **decide localmente** quando irrigar (mesmo sem internet), envia os dados por **Wi-Fi + MQTT sobre TLS** e mostra tudo em um **dashboard web**, que também permite controlar a bomba à distância.

- **Simulação no Wokwi:** https://wokwi.com/projects/479347468548827137
- **Dashboard:** [`dashboard/index.html`](dashboard/index.html)

---

## Arquitetura (4 camadas)

| Camada | Componentes |
|---|---|
| **Percepção** | DHT22 (temperatura e umidade do ar), sensor de umidade do solo (potenciômetro no Wokwi) |
| **Rede** | Wi-Fi 2.4 GHz do ESP32 + MQTT 3.1.1 sobre TLS (porta 8883) no broker público HiveMQ |
| **Processamento** | ESP32: regra de irrigação com histerese, modos automático/manual, validação de comandos |
| **Aplicação** | Dashboard web (MQTT via WebSocket seguro) + LEDs e buzzer no circuito |

```
[DHT22]──┐                                  ┌──> [Dashboard web]
[Solo]───┼──> [ESP32: decisão local] ──Wi-Fi/MQTT/TLS──> [Broker HiveMQ]
         │        │                                  └──< comandos (ON/OFF/AUTO, limite)
         │        └──> [Relé → Bomba]  [LEDs]  [Buzzer]
```

## Estrutura do repositório

```
smarthorta-iot/
├── firmware/smarthorta/
│   ├── smarthorta.ino        # código principal (no Wokwi: sketch.ino)
│   ├── config.h              # pinos, tópicos, tempos e limites
│   ├── certificados.h        # CA raiz (Amazon Root CA 1) para validar o broker via TLS
│   └── secrets.example.h     # MODELO de credenciais -> copie para secrets.h
├── wokwi/
│   ├── diagram.json          # circuito do Wokwi
│   └── libraries.txt         # bibliotecas usadas no Wokwi
├── dashboard/index.html      # painel web (abre direto no navegador)
├── docs/                     # relatório (PDF), proposta e imagens
├── .gitignore                # inclui secrets.h
└── README.md
```

## Como executar

### Opção 1 — Wokwi (recomendado)
1. Abra https://wokwi.com/projects/479347468548827137 e clique em ▶ (Start the simulation).
2. Aguarde no Monitor Serial as mensagens `[REDE] Wi-Fi conectado` e `[REDE] Conectando ao broker (TLS)... conectado!`.
3. Gire o potenciômetro ("Umidade do solo") para simular o solo secando ou molhando.

Para recriar o projeto do zero em outra conta: crie um projeto ESP32 vazio, cole o conteúdo de `smarthorta.ino` em `sketch.ino`, crie os arquivos `config.h`, `certificados.h` e `secrets.h` (a partir de `secrets.example.h`, com `WIFI_SSID "Wokwi-GUEST"` e senha vazia) e substitua `diagram.json` e `libraries.txt` pelos da pasta `wokwi/`.

### Opção 2 — Placa ESP32 física (Arduino IDE)
1. Instale o suporte a ESP32 (Espressif) na Arduino IDE.
2. Instale as bibliotecas **DHT sensor library for ESPx** (DHTesp) e **PubSubClient**.
3. Copie `firmware/smarthorta/secrets.example.h` para `secrets.h` e preencha o nome e a senha da sua rede Wi-Fi.
4. Monte o circuito conforme a tabela de pinos abaixo e grave `smarthorta.ino` na placa.

### Dashboard
Abra `dashboard/index.html` em qualquer navegador com internet (duplo clique no arquivo). Ele se conecta ao broker por `wss://broker.hivemq.com:8884/mqtt` e mostra os dados em tempo real, gráfico do histórico e botões de controle.

## Tabela de pinos

| Componente | Pino ESP32 | Observação |
|---|---|---|
| DHT22 (dados) | GPIO 15 | alimentação 3V3 |
| Sensor de umidade do solo / potenciômetro | GPIO 34 (ADC1) | entrada analógica 0–4095 |
| Módulo relé (IN) | GPIO 26 | aciona a bomba (LED azul no Wokwi) |
| LED verde — solo OK | GPIO 18 | resistor 220 Ω |
| LED vermelho — solo seco | GPIO 19 | resistor 220 Ω |
| LED amarelo — rede | GPIO 4 | aceso = conectado, piscando = sem conexão |
| Buzzer | GPIO 23 | alerta quando o solo seca |

## Comunicação MQTT

Broker: `broker.hivemq.com` · porta **8883 (TLS)** no ESP32 · porta 8884 (WebSocket seguro) no dashboard.

| Tópico | Direção | Conteúdo |
|---|---|---|
| `smarthorta/kz-rps-4793/dados` | ESP32 → dashboard | JSON a cada 2 s |
| `smarthorta/kz-rps-4793/status` | ESP32 → dashboard | `online` / `offline` (retido + Last Will) |
| `smarthorta/kz-rps-4793/cmd/bomba` | dashboard → ESP32 | `ON`, `OFF` ou `AUTO` |
| `smarthorta/kz-rps-4793/cmd/limite` | dashboard → ESP32 | inteiro de `10` a `90` |

Exemplo real de payload publicado:

```json
{"id":"smarthorta-01","solo":68,"temp":26.5,"umid":62.0,"bomba":false,"modo":"auto","limite":40,"sensor_ok":true,"uptime":14}
```

| Campo | Tipo | Significado |
|---|---|---|
| `id` | texto | identificador do dispositivo |
| `solo` | inteiro 0–100 | umidade do solo (%) |
| `temp` | número ou `null` | temperatura do ar (°C) |
| `umid` | número ou `null` | umidade relativa do ar (%) |
| `bomba` | booleano | bomba ligada? |
| `modo` | `auto` / `manual` | modo de operação |
| `limite` | inteiro 10–90 | abaixo deste valor o solo é considerado seco |
| `sensor_ok` | booleano | `false` se a última leitura do DHT22 falhou |
| `uptime` | inteiro | segundos desde que o ESP32 ligou |

## Regras de funcionamento

- **Automático:** liga a bomba se `solo < limite` e desliga quando `solo ≥ limite + 10` (histerese, evita liga-desliga).
- **Manual:** os comandos `ON`/`OFF` assumem o controle por até 2 minutos; depois o sistema volta sozinho ao automático.
- **Sem internet:** a regra roda no próprio ESP32. Se o Wi-Fi ou o broker caírem, a irrigação continua e o LED amarelo pisca; a reconexão é feita em segundo plano.

## Segurança

- Credenciais em `secrets.h`, fora do repositório (`.gitignore`), com modelo em `secrets.example.h`.
- MQTT sobre **TLS** com validação do certificado do broker (CA Amazon Root CA 1); dashboard via `wss://`.
- Validação de comandos: lista branca, tamanho máximo, faixa numérica e limite de taxa.
- Modo manual com tempo máximo e estado seguro (bomba desligada) ao ligar.
- Checklist OWASP IoT Top 10 completo no relatório (`docs/`).

## Uso de IA

O projeto foi desenvolvido com apoio do assistente Claude (Anthropic). O que foi produzido com esse apoio e como o grupo verificou cada parte está descrito na seção "Uso de ferramentas de IA" do relatório.
