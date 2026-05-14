# SNIN ESP32 — Архитектура

## Полный стек

```
┌────────────────────────────────────────────────────────────┐
│                      УРОВЕНЬ ПОЛЬЗОВАТЕЛЯ                   │
│                                                            │
│  Telegram (@Snindaobot)          Dashboard (snin.html)     │
│  ┌────────────────────────┐     ┌───────────────────────┐  │
│  │ /telemetry             │     │ Live relay status     │  │
│  │ /cmd <device> <action> │     │ Telemetry table       │  │
│  │ /chart temp 24         │     │ Charts (iframes)      │  │
│  │ /chart alerts 24       │     │ Alert history         │  │
│  │ /watch <device>        │     │ Auto-refresh 60s      │  │
│  └───────────┬────────────┘     └───────────┬───────────┘  │
│              │ Telegram API                  │ HTTP/WSS    │
└──────────────┼──────────────────────────────┼──────────────┘
               │                              │
┌──────────────┼──────────────────────────────┼──────────────┐
│              │  УРОВЕНЬ СЕРВЕРА              │              │
│              ▼                              ▼              │
│  ┌────────────────────┐     ┌──────────────────────────┐  │
│  │ bot.py (@Snindaobot)│     │ relay-snin.v2.site       │  │
│  │ (825 строк, v3.3)  │     │ (Nostr Relay + REST API) │  │
│  │                    │     │                          │  │
│  │ • Event poller 20s │←───→│ • WebSocket WSS :8198    │  │
│  │ • Push alerts      │     │ • kind:8010-8017 storage  │  │
│  │ • /cmd handler     │     │ • REST API /api/events   │  │
│  │ • Chart generator  │     │ • IPFS archive           │  │
│  │ • Watch/subscribe  │     │ • 21 NIPs               │  │
│  └────────────────────┘     └───────────┬──────────────┘  │
│                                         │ WSS             │
└─────────────────────────────────────────┼──────────────────┘
                                          │
┌─────────────────────────────────────────┼──────────────────┐
│                УРОВЕНЬ УСТРОЙСТВА        │                  │
│                                         ▼                  │
│  ┌──────────────────────────────────────────────┐          │
│  │  ESP32 (snin-esp32-firmware / main.cpp)      │          │
│  │                                               │          │
│  │  ┌─────────┐    ┌──────────┐    ┌─────────┐  │          │
│  │  │ DHT22   │───→│ main.cpp │───→│ WSS     │  │          │
│  │  │ GPIO4   │    │ (480ст)  │    │ Client  │  │          │
│  │  └─────────┘    └────┬─────┘    └────┬────┘  │          │
│  │                      │               │       │          │
│  │  ┌───────────────────┴───────────────┘       │          │
│  │  │ Потоки:                                   │          │
│  │  │ • kind:8010 → publishTelemetry() (60с)    │          │
│  │  │ • kind:8012 → handleCommand() (on event)  │          │
│  │  │ • kind:8011 → snedAlert() (on error)      │          │
│  │  │ • kind:8014 → publishRegistration() (1x)  │          │
│  │  └──────────────────────────────────────────┘          │
│  └──────────────────────────────────────────────┘          │
│                                                            │
│  Аппаратура:                                               │
│  ┌──────────────────────────────────────┐                  │
│  │ ESP32 (WROOM/S3)                     │                  │
│  │ GPIO4  → DHT22 DATA                  │                  │
│  │ 3.3V   → DHT22 VCC                   │                  │
│  │ GND    → DHT22 GND                   │                  │
│  │ GPIO35 → ADC (батарея, опционально)  │                  │
│  └──────────────────────────────────────┘                  │
└────────────────────────────────────────────────────────────┘
```

## Внутреннее устройство main.cpp

```
main.cpp (480 строк)
├── Определения (#define)
│   ├── DEVICE_ID       — уникальный ID устройства
│   ├── MEASURE_INTERVAL — частота публикаций (сек)
│   ├── RELAY_HOST/PORT — relay-snin.v2.site:443
│   └── DHT_PIN         — GPIO4
│
├── setup()
│   ├── dht.begin()
│   ├── connectWiFi()     — WPA2, 10с таймаут
│   └── connectRelay()    — WSS handshake
│
├── loop()
│   ├── ws.loop()         — WebSocket keepalive
│   ├── publishTelemetry()— каждые MEASURE_INTERVAL
│   └── delay(10)         — yield
│
├── connectWiFi()
│   ├── WiFi.begin()
│   ├── 20 попыток по 500мс
│   └── ESP.restart() при неудаче
│
├── connectRelay()
│   ├── ws.beginSSL()     — WSS relay-snin.v2.site
│   ├── subscribeCommands() — REQ kind:8012 #d=<device>
│   └── publishRegistration() — kind:8014
│
├── wsEvent(WStype_t)
│   ├── WStype_CONNECTED  → subscribe + register
│   ├── WStype_TEXT
│   │   ├── "EVENT" kind:8012 → handleCommand()
│   │   └── "OK" → подтверждение публикации
│   └── WStype_DISCONNECTED → reconnect
│
├── publishTelemetry()
│   ├── dht.readTemperature()
│   ├── dht.readHumidity()
│   ├── readBattery()     — ADC GPIO35
│   ├── Формирует JSON content
│   └── sendNostrEvent("8010", ...)
│
├── handleCommand(cmd)
│   ├── "read_sensor"     → publishTelemetry()
│   ├── "set_interval"    → MEASURE_INTERVAL =
│   ├── "reboot"           → ESP.restart()
│   ├── "pause"            → paused = true
│   ├── "resume"           → paused = false
│   └── "status"           → publishTelemetry(status)
│
├── sendNostrEvent(kind, content, tags)
│   ├── Собирает JSON event
│   ├── ("[\"EVENT\", {...}]")
│   └── ws.sendTXT(msg)
│
├── snedAlert(type, msg)
│   └── sendNostrEvent("8011", ...)
│
└── readBattery()
    └── analogRead(GPIO35) → %
```

## Поток данных (end-to-end)

```
1. REGISTRATION
   ESP32 → ["EVENT", {kind:8014, content:{fw,model,mac}}] → Relay
   Relay: сохраняет устройство
   Bot: видит в /telemetry

2. TELEMETRY (каждые 60с)
   ESP32 → ["EVENT", {kind:8010, content:{temp,hum,battery}, tags:[d,t,seq,batt]}] → Relay
   Bot (poll 20с):
       GET /api/events?kind=8010&since=last_check
       → новые events → sendMessage в Telegram
   Dashboard: обновляет таблицу

3. COMMAND (по запросу пользователя)
   TG: пользователь пишет /cmd esp32_01 read_sensor
   Bot → POST kind:8012 в relay
   ESP32 (подписка kind:8012 #d=esp32_01):
       ["EVENT", {kind:8012, content:{action:"read_sensor"}}]
       → Выполняет → публикует kind:8010 с результатом

4. ALERT (при ошибке)
   ESP32: DHT22 read failed
   → ["EVENT", {kind:8011, content:{alert:"sensor_fail",...}}] → Relay
   Bot: "🚨 esp32_01: sensor_fail — DHT read error"
```

## Сравнение с симуляцией

| Параметр | Симуляция (multi_relay.py) | Реальное устройство (ESP32) |
|----------|---------------------------|---------------------------|
| Где запущен | Python на сервере | ESP32 микроконтроллер |
| Данные | Случайные (random) | Реальные (DHT22) |
| Транспорт | REST API → relay | WSS напрямую |
| Питание | Нет | Батарея / USB |
| Команды | Не принимает | 6 команд kind:8012 |
| Регистрация | Не делает | kind:8014 при старте |
| Алерты | kind:8011 каждые 30 мин | По реальным событиям |
| Аппаратные ошибки | Нет | sensor_fail, battery_low |

## Дорожная карта

```
v1.0 ◄── Вы здесь
  Direct Mode: WiFi + WSS → relay
    └── DHT22, ADC battery, 6 команд

v1.1 ◄── Следующий шаг
  Mesh Mode: ESP32 ←ESP-NOW→ Bridge → WSS → Relay
    └── Глубокий сон, LoRa, BLE, mDNS

v1.2 ◄── В планах
  OTA: Обновление прошивки через kind:8013
    └── Шифрованный канал, rollback
```
