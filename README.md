# SNIN ESP32 Firmware

Прошивка для ESP32 — прямое подключение к Nostr relay. Публикует телеметрию с DHT22 (kind:8010), получает команды (kind:8012) от бота @Snindaobot.

## Возможности

- ✅ WiFi + WebSocket (WSS) → relay-snin.v2.site
- ✅ DHT22: температура + влажность
- ✅ Публикация kind:8010 каждые N секунд
- ✅ Приём команд kind:8012 (read_sensor, reboot, pause, set_interval, status)
- ✅ Алерты kind:8011 при ошибках датчика
- ✅ Регистрация kind:8014 при старте
- ✅ Мониторинг батареи через ADC

## Установка

### PlatformIO

```bash
git clone https://github.com/konantgit-sys/snin-esp32-firmware
cd snin-esp32-firmware
# Отредактируйте src/main.cpp: WiFi SSID, DEVICE_ID
pio run -t upload
pio device monitor
```

### Веб-прошивка (Chrome/Edge)

1. Откройте https://relay-snin.v2.site/firmware
2. Подключите ESP32 через USB
3. Нажмите "Прошить"
4. Подключитесь к WiFi `SNIN_Setup`, откройте 192.168.4.1
5. Введите ваш WiFi + настройки relay

## Команды через бота

После подключения устройство появится в @Snindaobot.

```
/cmd esp32_sensor_01 read_sensor    # принудительное чтение
/cmd esp32_sensor_01 set_interval 120  # изменить интервал
/cmd esp32_sensor_01 status         # полный статус
/cmd esp32_sensor_01 reboot         # перезагрузить
/cmd esp32_sensor_01 pause          # пауза
/cmd esp32_sensor_01 resume         # возобновить
```

## Подключение

| ESP32  | DHT22 |
|--------|-------|
| 3.3V   | VCC   |
| GND    | GND   |
| GPIO4  | DATA  |

## Стек

- ESP32 Arduino (PlatformIO)
- WiFi + WebSocketsClient
- ArduinoJson v7
- DHT sensor library
- Nostr NIP-80 (kind:8010–8017)

## См. также

- [NIP-80 спецификация](https://github.com/konantgit-sys/snin/blob/main/nip-80-snin.md)
- [SNIN Network](https://snin-network.v2.site)
- [SNIN Telegram Bot](https://t.me/Snindaobot)
- [Дашборд](https://cryter-dash.v2.site/snin.html)
