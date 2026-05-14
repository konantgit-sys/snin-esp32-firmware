# SNIN ESP32 Firmware v1.0

Прошивка для ESP32 — прямое подключение к Nostr relay. Публикует телеметрию с DHT22 (kind:8010), получает команды (kind:8012) от бота @Snindaobot.

```
ESP32 (DHT22) ──WiFi/WSS──→ Relay ──poll──→ @Snindaobot ──→ Telegram
                                ↑
                          ┌─────┴──────┐
                          │ /chart     │
                          │ /telemetry │
                          │ /cmd       │
                          └────────────┘
```

## Возможности

| Функция | Статус | kind |
|---------|--------|------|
| WiFi + WSS → relay | ✅ | — |
| DHT22 temp + hum | ✅ | — |
| Публикация телеметрии | ✅ | 8010 |
| Приём команд | ✅ | 8012 |
| Алерты при ошибках | ✅ | 8011 |
| Регистрация при старте | ✅ | 8014 |
| ADC батарея | ✅ | — |
| ESP-NOW mesh | ⏳ v1.1 | — |
| LoRa | ⏳ v1.1 | — |
| GPS трекер | ⏳ v1.1 | 8015 |

## Быстрый старт

### 1. Собрать схему

| ESP32 | DHT22 |
|-------|-------|
| 3.3V  | VCC   |
| GND   | GND   |
| GPIO4 | DATA  |

### 2. Настроить

Отредактировать `src/main.cpp`:

```cpp
const char* WIFI_SSID     = "YOUR_WIFI";
const char* WIFI_PASS     = "YOUR_PASSWORD";
const char* DEVICE_ID     = "esp32_sensor_01";
uint32_t   MEASURE_INTERVAL = 60;   // секунд между публикациями
```

### 3. Прошить

```bash
# PlatformIO
pio run -t upload
pio device monitor
```

### 4. Проверить

После загрузки ESP32:
1. Подключается к WiFi
2. Регистрируется в relay (kind:8014)
3. Начинает публиковать kind:8010 каждые 60с
4. В боте @Snindaobot: `/telemetry` — увидите устройство
5. `/chart temp 6` — график температуры

## Команды

### Telegram → ESP32 (/cmd в @Snindaobot)

| Команда | Действие ESP32 | Ответ |
|---------|---------------|-------|
| `/cmd <device> read_sensor` | Принудительное чтение DHT22 | kind:8010 |
| `/cmd <device> set_interval 120` | Сменить интервал (10-3600с) | ACK |
| `/cmd <device> reboot` | Перезагрузка ESP32 | ACK + reboot |
| `/cmd <device> pause` | Пауза публикаций | ACK |
| `/cmd <device> resume` | Возобновить | kind:8010 |
| `/cmd <device> status` | Полный статус | kind:8010 (status) |

### Telegram → Дашборд

| Команда | Что покажет |
|---------|------------|
| `/telemetry` | Все устройства и их показания |
| `/chart temp 24` | График температуры/влажности/батареи |
| `/chart alerts 24` | Гистограмма алертов |
| `/watch esp32_sensor_01` | Push-уведомления об алертах |

## Протокол NIP-80

Прошивка использует Nostr kinds: 8010–8017.

### kind:8010 — Telemetry
```json
{
  "temp": 23.5,
  "hum": 60.2,
  "battery": 85
}
```

### kind:8012 — Command
```json
// Запрос (от бота)
{"action": "read_sensor"}

// Ответ (от ESP32)
{"action": "read_sensor", "ok": true, "result": "sensor read triggered"}
```

### kind:8011 — Alert
```json
{"alert": "sensor_fail", "msg": "DHT read error", "ts": 1778728000}
```

## Архитектура связи

```
┌──────────────────────────────────────────────────────┐
│                    ESP32                              │
│  ┌─────────┐   ┌──────────┐   ┌──────────────────┐   │
│  │ DHT22   │→  │ main.cpp │→  │ WebSocketClient  │   │
│  │ GPIO4   │   │ 480строк  │   │ WSS → relay      │   │
│  └─────────┘   └────┬─────┘   └──────────────────┘   │
│                     │                                  │
│              ┌──────┴──────┐                          │
│              │ kind:8010   │                          │
│              │ kind:8011   │                          │
│              │ kind:8014   │                          │
│              └──────┬──────┘                          │
└─────────────────────┼────────────────────────────────┘
                      │ WSS
               ┌──────┴──────┐
               │  Relay V2   │
               │ relay-snin  │
               └──────┬──────┘
                      │ HTTP poll (20s)
               ┌──────┴──────┐
               │ @Snindaobot │
               │ bot.py      │
               └──────┬──────┘
                      │ Telegram
                      ▼
                 Пользователь
```

## Структура проекта

```
snin-esp32-firmware/
├── platformio.ini       # 3 среды: esp32dev, esp32-s3, esp8266
├── README.md            # Этот файл
├── ARCHITECTURE.md      # Полная архитектура
├── src/
│   └── main.cpp         # Прошивка (480 строк)
│       ├── setup()      # WiFi + WSS connect
│       ├── loop()       # publish + command check
│       ├── publishTelemetry()   # kind:8010
│       ├── subscribeCommands()  # kind:8012 filter
│       ├── handleCommand()      # 6 команд
│       └── snedAlert()          # kind:8011
└── firmware.html        # Веб-страница прошивки
```

## Зависимости

- **Платформа:** ESP32 (WROOM / S3)
- **Фреймворк:** Arduino (PlatformIO)
- **Библиотеки:**
  - `bblanchon/ArduinoJson @ ^7.0.3`
  - `adafruit/DHT sensor library @ ^1.4.6`
  - `links2004/WebSockets @ ^2.4.1`

## Ссылки

- [SNIN Network](https://snin-network.v2.site)
- [Документация прошивки](https://relay-snin.v2.site/firmware)
- [NIP-80 спецификация](https://github.com/konantgit-sys/snin/blob/main/nip-80-snin.md)
- [Telegram Bot @Snindaobot](https://t.me/Snindaobot)
- [Дашборд](https://cryter-dash.v2.site/snin.html)

## Лицензия

MIT — см. [LICENSE](LICENSE)
