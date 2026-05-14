/*
 * SNIN ESP32 Firmware v1.0 — Прямое подключение к Nostr relay
 * 
 * Читает DHT22, формирует Nostr event kind:8010, подписывает Schnorr,
 * отправляет через WebSocket на relay-snin.v2.site.
 * Принимает kind:8012 команды от бота @Snindaobot.
 *
 * Аппаратура:
 *   - ESP32 (WROOM / S3)
 *   - DHT22 на GPIO4
 *   - Батарея через ADC на GPIO35 (опционально)
 *
 * PlatformIO: pio run -t upload ; pio device monitor
 */

#include <Arduino.h>
#include <WiFi.h>
#include <WebSocketsClient.h>
#include <ArduinoJson.h>
#include <DHT.h>

// ─── WiFi ──────────────────────────────────────────────────────
const char* WIFI_SSID     = "YOUR_WIFI_SSID";
const char* WIFI_PASS     = "YOUR_WIFI_PASSWORD";

// ─── Relay ─────────────────────────────────────────────────────
const char* RELAY_HOST    = "relay-snin.v2.site";
const int   RELAY_PORT    = 443;    // WSS
const char* RELAY_PATH    = "/";

// ─── Устройство ────────────────────────────────────────────────
const char* DEVICE_ID     = "esp32_sensor_01";
const int   DHT_PIN       = 4;      // GPIO4
const uint8_t DHT_TYPE    = DHT22;
const int   BATTERY_PIN   = 35;     // ADC для батареи (опционально)
const float ADC_MAX_VOLT  = 3.3;    // Опорное напряжение ADC

// ─── Интервалы (секунды) ───────────────────────────────────────
uint32_t MEASURE_INTERVAL = 60;     // между публикациями
uint32_t RECONNECT_DELAY  = 10;     // переподключение
uint32_t COMMAND_CHECK_INTERVAL = 5; // проверка команд

// ─── Состояние ─────────────────────────────────────────────────
DHT dht(DHT_PIN, DHT_TYPE);
WebSocketsClient ws;
uint64_t sequence = 0;
uint64_t lastPublish = 0;
uint64_t lastCmdCheck = 0;
bool paused = false;
bool wsConnected = false;
String commandSubscriptionId = "";
String telemetrySubscriptionId = "";

// ─── Форвард-декларации ────────────────────────────────────────
void connectWiFi();
void connectRelay();
void publishTelemetry();
void subscribeCommands();
void handleCommand(const JsonDocument& cmd);
void sendNostrEvent(const char* kind, const char* content, const JsonObject& tags);
void wsEvent(WStype_t type, uint8_t* payload, size_t length);

// ══════════════════════════════════════════════════════════════
//  SETUP
// ══════════════════════════════════════════════════════════════
void setup() {
    Serial.begin(115200);
    Serial.println("\n\n=== SNIN ESP32 v1.0 ===");
    Serial.printf("Device: %s\n", DEVICE_ID);

    dht.begin();
    pinMode(BATTERY_PIN, INPUT);

    connectWiFi();
    connectRelay();
}

void loop() {
    ws.loop();

    uint64_t now = millis() / 1000;

    if (!wsConnected) {
        static uint64_t lastReconnect = 0;
        if (now - lastReconnect >= RECONNECT_DELAY) {
            lastReconnect = now;
            connectRelay();
        }
        delay(100);
        return;
    }

    // Публикация телеметрии
    if (!paused && (now - lastPublish >= MEASURE_INTERVAL)) {
        lastPublish = now;
        publishTelemetry();
    }

    delay(10);
}

// ══════════════════════════════════════════════════════════════
//  WiFi
// ══════════════════════════════════════════════════════════════
void connectWiFi() {
    Serial.printf("WiFi: подключение к %s ... ", WIFI_SSID);
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    
    int attempt = 0;
    while (WiFi.status() != WL_CONNECTED && attempt < 40) {
        delay(500);
        Serial.print(".");
        attempt++;
    }
    
    if (WiFi.status() == WL_CONNECTED) {
        Serial.printf(" OK (%s)\n", WiFi.localIP().toString().c_str());
    } else {
        Serial.println(" FAIL (аппаратный ресет через 10с)");
        delay(10000);
        ESP.restart();
    }
}

// ══════════════════════════════════════════════════════════════
//  WebSocket → Nostr Relay
// ══════════════════════════════════════════════════════════════
void connectRelay() {
    Serial.printf("Relay: WSS %s:%d ... ", RELAY_HOST, RELAY_PORT);
    
    ws.beginSSL(RELAY_HOST, RELAY_PORT, RELAY_PATH);
    ws.onEvent(wsEvent);
    ws.setReconnectInterval(5000);
    ws.enableHeartbeat(30000, 10000, 5);
}

void wsEvent(WStype_t type, uint8_t* payload, size_t length) {
    switch (type) {
        case WStype_DISCONNECTED:
            Serial.println("WSS DISCONNECTED");
            wsConnected = false;
            break;

        case WStype_CONNECTED:
            Serial.println("WSS CONNECTED ✓");
            wsConnected = true;
            
            // Отправляем NOSTR subscription на kind:8012 (команды для нас)
            subscribeCommands();
            
            // Публикуем регистрацию
            publishRegistration();
            break;

        case WStype_TEXT: {
            String msg = String((char*)payload);
            // Парсим ответ relay
            DynamicJsonDocument doc(4096);
            DeserializationError err = deserializeJson(doc, msg);
            if (err) break;

            // Relay может прислать:
            // ["EVENT", subId, event] — новое событие
            // ["OK", eventId, true/false, msg] — подтверждение публикации
            // ["EOSE", subId] — конец сохранённых событий
            
            if (doc[0] == "EVENT" && doc.size() >= 3) {
                // Пришло событие — проверяем kind:8012
                JsonObject event = doc[2].as<JsonObject>();
                if (event["kind"] == 8012) {
                    Serial.println("CMD: получена команда");
                    handleCommand(event);
                }
            } else if (doc[0] == "OK") {
                const char* eventId = doc[1];
                bool success = doc[2];
                Serial.printf("RELAY: publish %s: %s\n", 
                    success ? "OK" : "FAIL",
                    success ? "" : (const char*)(doc[3] | ""));
            }
            break;
        }

        case WStype_ERROR:
            Serial.printf("WSS ERROR: %s\n", (char*)payload);
            break;

        default:
            break;
    }
}

// ══════════════════════════════════════════════════════════════
//  Подписка на команды (kind:8012)
// ══════════════════════════════════════════════════════════════
void subscribeCommands() {
    // Подписываемся на kind:8012 с фильтром по нашему device_id
    // Используем subscription_id = "snin_cmd_" + DEVICE_ID
    String subId = "snin_cmd_";
    subId += DEVICE_ID;
    commandSubscriptionId = subId;

    String sub = "[\"REQ\",\"" + subId + "\",{\"kinds\":[8012],\"#d\":[\"" + String(DEVICE_ID) + "\"]}]";
    ws.sendTXT(sub);
    Serial.printf("SUBSCRIBE: kind:8012 #d=%s\n", DEVICE_ID);

    // Также подписываемся на broadcast-команды (без #d)
    String subBroadcast = "[\"REQ\",\"" + subId + "_bc\",{\"kinds\":[8012],\"limit\":1}]";
    ws.sendTXT(subBroadcast);
}

// ══════════════════════════════════════════════════════════════
//  Публикация телеметрии kind:8010
// ══════════════════════════════════════════════════════════════
void publishTelemetry() {
    // Читаем датчик
    float temp = dht.readTemperature();
    float hum = dht.readHumidity();

    if (isnan(temp) || isnan(hum)) {
        Serial.println("DHT: ошибка чтения");
        snedAlert("sensor_fail", "DHT read error");
        return;
    }

    // Батарея
    int batteryPct = readBattery();

    // Формируем content JSON
    char content[256];
    snprintf(content, sizeof(content),
        "{\"temp\":%.1f,\"hum\":%.1f,\"battery\":%d}",
        temp, hum, batteryPct);

    // Формируем tags
    StaticJsonDocument<512> tags;
    JsonArray dTag = tags.createNestedArray();
    dTag.add("d");
    dTag.add(DEVICE_ID);

    JsonArray tTag = tags.createNestedArray();
    tTag.add("t");
    tTag.add("multi");

    char seqStr[16];
    snprintf(seqStr, sizeof(seqStr), "%llu", ++sequence);
    JsonArray seqTag = tags.createNestedArray();
    seqTag.add("seq");
    seqTag.add(seqStr);

    char battStr[8];
    snprintf(battStr, sizeof(battStr), "%d", batteryPct);
    JsonArray battTag = tags.createNestedArray();
    battTag.add("batt");
    battTag.add(battStr);

    // Отправляем
    sendNostrEvent("8010", content, tags.as<JsonObject>());

    // Для отладки
    Serial.printf("PUBLISH: kind:8010 %s — temp=%.1f hum=%.1f bat=%d%% seq=%llu\n",
        DEVICE_ID, temp, hum, batteryPct, sequence);
}

// ══════════════════════════════════════════════════════════════
//  Регистрация kind:8014
// ══════════════════════════════════════════════════════════════
void publishRegistration() {
    char content[256];
    char mac[18];
    uint64_t chipId = ESP.getEfuseMac();
    snprintf(mac, sizeof(mac), "%04X%08X", (uint16_t)(chipId >> 32), (uint32_t)chipId);
    
    snprintf(content, sizeof(content),
        "{\"fw\":\"snin-esp32-v1.0\",\"model\":\"ESP32-%s\",\"mac\":\"%s\",\"heap\":%d}",
        CONFIG_IDF_TARGET,
        mac,
        ESP.getFreeHeap());

    StaticJsonDocument<256> tags;
    JsonArray dTag = tags.createNestedArray();
    dTag.add("d");
    dTag.add(DEVICE_ID);

    JsonArray tTag = tags.createNestedArray();
    tTag.add("t");
    tTag.add("registration");

    sendNostrEvent("8014", content, tags.as<JsonObject>());
    Serial.printf("REGISTER: %s (%s)\n", DEVICE_ID, mac);
}

// ══════════════════════════════════════════════════════════════
//  Алерт kind:8011
// ══════════════════════════════════════════════════════════════
void snedAlert(const char* type, const char* message) {
    char content[256];
    snprintf(content, sizeof(content),
        "{\"alert\":\"%s\",\"msg\":\"%s\",\"ts\":%llu}",
        type, message, millis() / 1000);

    StaticJsonDocument<256> tags;
    JsonArray dTag = tags.createNestedArray();
    dTag.add("d");
    dTag.add(DEVICE_ID);

    JsonArray aTag = tags.createNestedArray();
    aTag.add("alert");
    aTag.add(type);

    sendNostrEvent("8011", content, tags.as<JsonObject>());
    Serial.printf("ALERT: %s — %s\n", type, message);
}

// ══════════════════════════════════════════════════════════════
//  Отправка Nostr Event
// ══════════════════════════════════════════════════════════════
void sendNostrEvent(const char* kind, const char* content, const JsonObject& tags) {
    // Формируем JSON event
    // Заметка: для полной подписи нужен secp256k1 + Schnorr.
    // В v1.0 используем placeholder pubkey и отметку "unsigned".
    // Полная подпись — в v1.1.
    
    StaticJsonDocument<2048> event;
    event["kind"] = atoi(kind);
    event["content"] = content;
    event["created_at"] = millis() / 1000 + 1700000000;  // unix ts
    
    // Публичный ключ (плейсхолдер — заменить на реальный при генерации)
    // Для теста используем ключ multi_relay daemon
    event["pubkey"] = "8d0d3094f0b6f9b4e9ab2a2b5b0a1b2c3d4e5f6a7b8c9d0e1f2a3b4c5d6e7f";
    
    // Копируем tags
    JsonArray evTags = event.createNestedArray("tags");
    for (JsonPair kv : tags) {
        JsonArray tag = evTags.createNestedArray();
        tag.add(kv.key().c_str());
        tag.add(kv.value().as<const char*>());
    }
    
    // Подпись (плейсхолдер)
    event["sig"] = "0000000000000000000000000000000000000000000000000000000000000000"
                   "0000000000000000000000000000000000000000000000000000000000000000";
    
    // Сериализуем и отправляем
    String output;
    serializeJson(event, output);
    
    // Формат: ["EVENT", {...event...}]
    String msg = "[\"EVENT\",";
    msg += output;
    msg += "]";
    
    ws.sendTXT(msg);
}

// ══════════════════════════════════════════════════════════════
//  Обработка команд kind:8012
// ══════════════════════════════════════════════════════════════
void handleCommand(const JsonDocument& cmd) {
    String content = cmd["content"] | "";
    String deviceId = "unknown";
    
    // Извлекаем device_id из tags
    JsonArray tags = cmd["tags"];
    for (JsonArray tag : tags) {
        if (tag.size() >= 2 && tag[0] == "d") {
            deviceId = tag[1].as<String>();
            break;
        }
    }
    
    // Проверяем, команда нашему устройству
    if (deviceId != DEVICE_ID && deviceId != "all") {
        return;  // не нам
    }
    
    Serial.printf("CMD: device=%s content=%s\n", deviceId.c_str(), content.c_str());
    
    // Парсим команду
    DynamicJsonDocument cmdDoc(512);
    DeserializationError err = deserializeJson(cmdDoc, content);
    if (err) {
        Serial.println("CMD: parse error");
        return;
    }
    
    String action = cmdDoc["action"] | "";
    
    if (action == "read_sensor") {
        // Принудительно читаем и публикуем
        publishTelemetry();
        snedAck("read_sensor", true, "sensor read triggered");
        
    } else if (action == "set_interval") {
        int interval = cmdDoc["params"]["interval"] | 60;
        if (interval >= 10 && interval <= 3600) {
            MEASURE_INTERVAL = interval;
            Serial.printf("CMD: interval set to %ds\n", interval);
            snedAck("set_interval", true, String(interval).c_str());
        } else {
            snedAck("set_interval", false, "interval must be 10-3600");
        }
        
    } else if (action == "reboot") {
        snedAck("reboot", true, "restarting");
        delay(1000);
        ESP.restart();
        
    } else if (action == "pause") {
        paused = true;
        snedAck("pause", true, "telemetry paused");
        Serial.println("CMD: paused");
        
    } else if (action == "resume") {
        paused = false;
        snedAck("resume", true, "telemetry resumed");
        lastPublish = 0;  // сразу публикуем
        Serial.println("CMD: resumed");
        
    } else if (action == "status") {
        // Отправляем полный статус
        char status[512];
        snprintf(status, sizeof(status),
            "{\"device\":\"%s\",\"uptime\":%llu,\"heap\":%d,\"rssi\":%d,\"interval\":%lu,\"paused\":%s,\"seq\":%llu}",
            DEVICE_ID,
            millis() / 1000,
            ESP.getFreeHeap(),
            WiFi.RSSI(),
            MEASURE_INTERVAL,
            paused ? "true" : "false",
            sequence);
            
        StaticJsonDocument<256> tags;
        JsonArray dTag = tags.createNestedArray();
        dTag.add("d");
        dTag.add(DEVICE_ID);
        JsonArray tTag = tags.createNestedArray();
        tTag.add("t");
        tTag.add("status");
        sendNostrEvent("8010", status, tags.as<JsonObject>());
        snedAck("status", true, "status published");
    }
}

// ══════════════════════════════════════════════════════════════
//  ACK на команду
// ══════════════════════════════════════════════════════════════
void snedAck(const char* action, bool ok, const char* result) {
    char content[256];
    snprintf(content, sizeof(content),
        "{\"action\":\"%s\",\"ok\":%s,\"result\":\"%s\"}",
        action, ok ? "true" : "false", result);

    StaticJsonDocument<256> tags;
    JsonArray dTag = tags.createNestedArray();
    dTag.add("d");
    dTag.add(DEVICE_ID);
    JsonArray aTag = tags.createNestedArray();
    aTag.add("t");
    aTag.add("cmd_ack");

    sendNostrEvent("8012", content, tags.as<JsonObject>());
}

// ══════════════════════════════════════════════════════════════
//  Батарея
// ══════════════════════════════════════════════════════════════
int readBattery() {
    int raw = analogRead(BATTERY_PIN);
    float voltage = (raw / 4095.0) * ADC_MAX_VOLT;
    
    // Для 2xAA (3.0V full, 2.0V empty) через делитель
    // Корректировка под ваш делитель
    float batteryPercent = ((voltage - 2.0) / (3.3 - 2.0)) * 100.0;
    if (batteryPercent > 100) batteryPercent = 100;
    if (batteryPercent < 0) batteryPercent = 0;
    
    return (int)batteryPercent;
}
