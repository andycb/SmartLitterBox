/*
 * MQTT Handler for Home Assistant Auto-Discovery
 * 
 * Manages:
 * - Home Assistant MQTT discovery
 * - Graceful offline publish queue
 * - Exponential backoff reconnection
 * - WiFi monitoring and recovery
 * - Diagnostic sensor publishing (WiFi RSSI, MQTT status, uptime)
 */

#ifndef MQTT_HANDLER_H
#define MQTT_HANDLER_H

#include <PubSubClient.h>
#include <ESP8266WiFi.h>
#include <time.h>
#include "config.h"

// Maximum length for MQTT topic and payload
#define MQTT_MAX_TOPIC_LEN    256
#define MQTT_MAX_PAYLOAD_LEN  256

// MQTT connection states (renamed to avoid PubSubClient macro conflicts)
enum MqttConnectionState {
    MQTT_STATE_DISCONNECTED,
    MQTT_STATE_CONNECTING,
    MQTT_STATE_CONNECTED,
    MQTT_STATE_FAILED
};

// WiFi connection states
enum WiFiConnectionState {
    WIFI_STATE_DISCONNECTED,
    WIFI_STATE_CONNECTING,
    WIFI_STATE_CONNECTED
};

// Publish queue entry structure
struct MqttQueueEntry {
    unsigned long timestamp;
    char topic[MQTT_MAX_TOPIC_LEN];
    char payload[MQTT_MAX_PAYLOAD_LEN];
    bool retain;
};

class MqttHandler {
private:
    // Use WiFiClient for plain MQTT (port 1883), WiFiClientSecure for TLS (port 8883)
    #if HA_MQTT_PORT == 1883
        WiFiClient espClient;
    #else
        WiFiClientSecure espClient;
    #endif
    
    PubSubClient mqttClient;
    
    // Connection state tracking
    MqttConnectionState mqttState;
    WiFiConnectionState wifiState;
    unsigned long lastMqttAttempt;
    unsigned long lastWifiAttempt;
    unsigned long deviceStartTime;
    int mqttBackoffLevel;  // 0-4 for 30s, 1m, 2m, 5m, 10m
    int wifiBackoffLevel;
    
    // Queue for offline publishes
    MqttQueueEntry publishQueue[MQTT_QUEUE_DEPTH];
    int queueHead;
    int queueTail;
    int queueCount;
    
    // Diagnostics tracking
    unsigned long lastDiagnosticPublish;
    int lastWifiRssi;
    
    // Private methods
    void sendDiscoveryPayload(const char* sensorId, const char* sensorName, const char* unitOfMeasure);
    void drainQueue();
    void handleMqttReconnect();
    void handleWifiReconnect();
    unsigned long getBackoffDelay(int backoffLevel, bool isWifi);
    void addJitter(unsigned long& delay);
    void publishDiagnostics();
    bool publishTopic(const char* topic, const char* payload, bool retain);
    
public:
    MqttHandler();
    
    // Lifecycle methods
    void setup();
    void tick();  // Called every loop iteration
    
    // Publishing methods
    void publishReading(float catWeight, float poopWeight, int durationMs, unsigned long ageMs = 0);
    
    // Status methods
    MqttConnectionState getConnectionStatus();
    bool isConnected();
    unsigned long getUptime();
};

// Constructor
MqttHandler::MqttHandler() 
    : mqttClient(espClient),
      mqttState(MQTT_STATE_DISCONNECTED),
      wifiState(WIFI_STATE_DISCONNECTED),
      lastMqttAttempt(0),
      lastWifiAttempt(0),
      deviceStartTime(0),
      mqttBackoffLevel(0),
      wifiBackoffLevel(0),
      queueHead(0),
      queueTail(0),
      queueCount(0),
      lastDiagnosticPublish(0),
      lastWifiRssi(0)
{
}

// Setup WiFi and MQTT connections
void MqttHandler::setup() {
    Serial.println("Initializing MQTT Handler...");
    
    // Record device start time
    deviceStartTime = millis();
    
    // Configure MQTT client
    mqttClient.setServer(HA_MQTT_SERVER, HA_MQTT_PORT);
    mqttClient.setBufferSize(512);
    
    // Configure TLS for secure MQTT (port 8883 and above)
    #if HA_MQTT_PORT >= 8883
        Serial.println("Using secure MQTT (TLS)");
        espClient.setInsecure();  // Skip cert validation for self-signed certs
    #else
        Serial.println("Using plain MQTT (no TLS)");
    #endif
    
    // Initial WiFi connection attempt
    handleWifiReconnect();
}

// Main tick function - called each loop iteration
void MqttHandler::tick() {
    // Check WiFi status
    if (WiFi.status() != WL_CONNECTED) {
        if (wifiState == WIFI_STATE_CONNECTED) {
            Serial.println("WiFi disconnected!");
            wifiState = WIFI_STATE_DISCONNECTED;
            lastWifiAttempt = millis();
            wifiBackoffLevel = 0;  // Reset backoff on detection
        }
        handleWifiReconnect();
    } else if (wifiState != WIFI_STATE_CONNECTED) {
        wifiState = WIFI_STATE_CONNECTED;
        wifiBackoffLevel = 0;
        Serial.print("WiFi reconnected. IP: ");
        Serial.println(WiFi.localIP());
    }
    
    // Check MQTT status
    if (!mqttClient.connected()) {
        if (mqttState == MQTT_STATE_CONNECTED) {
            Serial.println("MQTT disconnected!");
            mqttState = MQTT_STATE_DISCONNECTED;
            lastMqttAttempt = millis();
        }
        if (wifiState == WIFI_STATE_CONNECTED) {
            handleMqttReconnect();
        }
    } else if (mqttState != MQTT_STATE_CONNECTED) {
        mqttState = MQTT_STATE_CONNECTED;
        mqttBackoffLevel = 0;
        Serial.println("MQTT connected!");
        
    // Send discovery payloads on successful connection
        sendDiscoveryPayload("cat_weight", "Cat Weight", "kg");
        sendDiscoveryPayload("poop_weight", "Poop Weight", "kg");
        sendDiscoveryPayload("poop_duration", "Poop Duration", "s");
        sendDiscoveryPayload("data_age", "Data Age", "s");
        sendDiscoveryPayload("wifi_rssi", "WiFi Signal", "dBm");
        sendDiscoveryPayload("mqtt_connected", "MQTT Connected", "");
        sendDiscoveryPayload("uptime", "Uptime", "s");
        
        // Drain any queued publishes
        drainQueue();
    }
    
    // Keep MQTT connection alive
    if (mqttClient.connected()) {
        mqttClient.loop();
        
        // Publish diagnostics at regular intervals
        unsigned long now = millis();
        if (now - lastDiagnosticPublish >= DIAGNOSTIC_PUBLISH_INTERVAL) {
            publishDiagnostics();
            lastDiagnosticPublish = now;
        }
    }
}

// Publish a cat litter usage reading
void MqttHandler::publishReading(float catWeight, float poopWeight, int durationMs, unsigned long ageMs) {
    char topic[MQTT_MAX_TOPIC_LEN];
    char payload[MQTT_MAX_PAYLOAD_LEN];
    
    Serial.print("Publishing reading (age: ");
    Serial.print(ageMs / 1000);
    Serial.println(" s)");
    
    // Publish cat weight
    snprintf(topic, sizeof(topic), "%s/sensor/%s/cat_weight/state", 
             HA_MQTT_TOPIC_PREFIX, DEVICE_UNIQUE_ID);
    snprintf(payload, sizeof(payload), "%.2f", catWeight);
    publishTopic(topic, payload, false);
    
    // Publish poop weight
    snprintf(topic, sizeof(topic), "%s/sensor/%s/poop_weight/state", 
             HA_MQTT_TOPIC_PREFIX, DEVICE_UNIQUE_ID);
    snprintf(payload, sizeof(payload), "%.2f", poopWeight);
    publishTopic(topic, payload, false);
    
    // Publish duration
    snprintf(topic, sizeof(topic), "%s/sensor/%s/poop_duration/state", 
             HA_MQTT_TOPIC_PREFIX, DEVICE_UNIQUE_ID);
    snprintf(payload, sizeof(payload), "%d", durationMs / 1000);  // Convert to seconds
    publishTopic(topic, payload, false);
    
    // Publish data age (time from queue to publish)
    snprintf(topic, sizeof(topic), "%s/sensor/%s/data_age/state", 
             HA_MQTT_TOPIC_PREFIX, DEVICE_UNIQUE_ID);
    snprintf(payload, sizeof(payload), "%lu", ageMs / 1000);  // Convert to seconds
    publishTopic(topic, payload, false);
}

// Internal: Publish a single topic (handles queuing if offline)
bool MqttHandler::publishTopic(const char* topic, const char* payload, bool retain) {
    if (mqttState != MQTT_STATE_CONNECTED) {
        // Queue the publish
        if (queueCount < MQTT_QUEUE_DEPTH) {
            int newTail = (queueTail + 1) % MQTT_QUEUE_DEPTH;
            strncpy(publishQueue[queueTail].topic, topic, MQTT_MAX_TOPIC_LEN - 1);
            strncpy(publishQueue[queueTail].payload, payload, MQTT_MAX_PAYLOAD_LEN - 1);
            publishQueue[queueTail].retain = retain;
            publishQueue[queueTail].timestamp = millis();
            queueTail = newTail;
            queueCount++;
            
            if (queueCount > MQTT_QUEUE_DEPTH - 5) {
                Serial.print("Warning: Publish queue depth: ");
                Serial.println(queueCount);
            }
        } else {
            // Queue full - drop oldest entry
            Serial.println("ERROR: Publish queue full, dropping oldest entry");
            queueHead = (queueHead + 1) % MQTT_QUEUE_DEPTH;
            
            // Add new entry
            strncpy(publishQueue[queueTail].topic, topic, MQTT_MAX_TOPIC_LEN - 1);
            strncpy(publishQueue[queueTail].payload, payload, MQTT_MAX_PAYLOAD_LEN - 1);
            publishQueue[queueTail].retain = retain;
            publishQueue[queueTail].timestamp = millis();
            queueTail = (queueTail + 1) % MQTT_QUEUE_DEPTH;
        }
        return false;
    }
    
    // MQTT connected - publish immediately
    bool success = mqttClient.publish(topic, payload, retain);
    if (!success) {
        Serial.print("Failed to publish to: ");
        Serial.println(topic);
    }
    return success;
}

// Internal: Send Home Assistant discovery payload
void MqttHandler::sendDiscoveryPayload(const char* sensorId, const char* sensorName, const char* unitOfMeasure) {
    char topic[MQTT_MAX_TOPIC_LEN];
    char payload[512];  // Discovery payload can be large
    
    snprintf(topic, sizeof(topic), "%s/sensor/%s/%s/config", 
             HA_MQTT_TOPIC_PREFIX, DEVICE_UNIQUE_ID, sensorId);
    
    // Build discovery JSON
    snprintf(payload, sizeof(payload),
        "{\"name\":\"%s\","
        "\"unique_id\":\"%s_%s\","
        "\"state_topic\":\"%s/sensor/%s/%s/state\","
        "\"device\":{\"identifiers\":[\"%s\"],\"name\":\"%s\",\"manufacturer\":\"%s\",\"sw_version\":\"%s\",\"hw_version\":\"%s\"}",
        sensorName,
        DEVICE_UNIQUE_ID, sensorId,
        HA_MQTT_TOPIC_PREFIX, DEVICE_UNIQUE_ID, sensorId,
        DEVICE_UNIQUE_ID, DEVICE_NAME, DEVICE_MANUFACTURER, DEVICE_VERSION, DEVICE_HARDWARE_VERSION);
    
    // Add unit of measurement if specified
    if (strlen(unitOfMeasure) > 0) {
        strncat(payload, ",\"unit_of_meas\":\"", sizeof(payload) - strlen(payload) - 1);
        strncat(payload, unitOfMeasure, sizeof(payload) - strlen(payload) - 1);
        strncat(payload, "\"", sizeof(payload) - strlen(payload) - 1);
    }
    
    // Close the JSON object
    strncat(payload, "}", sizeof(payload) - strlen(payload) - 1);
    
    // Debug: Print the discovery payload
    Serial.print("Discovery topic: ");
    Serial.println(topic);
    Serial.print("Discovery payload: ");
    Serial.println(payload);
    
    // Publish with retain flag (true = broker keeps this message for new subscribers)
    bool success = mqttClient.publish(topic, payload, true);
    Serial.print("Published discovery: ");
    Serial.print(sensorId);
    Serial.print(" - ");
    Serial.println(success ? "OK" : "FAILED");
}

// Internal: Drain the publish queue
void MqttHandler::drainQueue() {
    int published = 0;
    unsigned long drainStart = millis();
    
    while (queueCount > 0 && (millis() - drainStart) < 5000) {  // Max 5 second drain window
        // Calculate age for queued items (time spent in queue + any prior age)
        unsigned long currentAge_ms = millis() - publishQueue[queueHead].timestamp;
        
        // For data_age topics, use the calculated age; for others, use stored payload
        char publishPayload[MQTT_MAX_PAYLOAD_LEN];
        bool isAgeMetric = false;
        
        if (strstr(publishQueue[queueHead].topic, "/data_age/state") != NULL) {
            isAgeMetric = true;
            snprintf(publishPayload, sizeof(publishPayload), "%lu", currentAge_ms / 1000);
        } else {
            strncpy(publishPayload, publishQueue[queueHead].payload, sizeof(publishPayload) - 1);
            publishPayload[sizeof(publishPayload) - 1] = '\0';
        }
        
        if (!mqttClient.publish(publishQueue[queueHead].topic, publishPayload, publishQueue[queueHead].retain)) {
            // Publish failed, stop draining
            break;
        }
        
        if (isAgeMetric) {
            Serial.print("Drained queued publish (");
            Serial.print(currentAge_ms / 1000);
            Serial.print("s old): ");
            Serial.println(publishQueue[queueHead].topic);
        } else {
            Serial.print("Drained queued publish: ");
            Serial.println(publishQueue[queueHead].topic);
        }
        
        queueHead = (queueHead + 1) % MQTT_QUEUE_DEPTH;
        queueCount--;
        published++;
        
        // Small delay between publishes to respect rate limits
        delay(50);
    }
    
    if (published > 0) {
        Serial.print("Drained ");
        Serial.print(published);
        Serial.println(" queued publishes");
    }
}

// Internal: Handle MQTT reconnection with exponential backoff
void MqttHandler::handleMqttReconnect() {
    if (mqttState == MQTT_STATE_CONNECTING || mqttState == MQTT_STATE_CONNECTED) {
        return;  // Already connecting or connected
    }
    
    if (wifiState != WIFI_STATE_CONNECTED) {
        return;  // WiFi not connected
    }
    
    unsigned long now = millis();
    unsigned long backoffDelay = getBackoffDelay(mqttBackoffLevel, false);
    
    if (now - lastMqttAttempt < backoffDelay) {
        return;  // Not time to retry yet
    }
    
    Serial.print("Attempting MQTT connection (backoff level ");
    Serial.print(mqttBackoffLevel);
    Serial.println(")");
    
    // Debug output
    Serial.print("  Server: ");
    Serial.print(HA_MQTT_SERVER);
    Serial.print(":");
    Serial.println(HA_MQTT_PORT);
    Serial.print("  Client ID: ");
    Serial.println(DEVICE_UNIQUE_ID);
    Serial.print("  Username: ");
    Serial.println(HA_MQTT_USERNAME);
    Serial.print("  Password: ");
    Serial.println(HA_MQTT_PASSWORD);
    
    mqttState = MQTT_STATE_CONNECTING;
    lastMqttAttempt = now;
    
    // Attempt connection
    if (mqttClient.connect(DEVICE_UNIQUE_ID, HA_MQTT_USERNAME, HA_MQTT_PASSWORD)) {
        mqttState = MQTT_STATE_CONNECTED;
        mqttBackoffLevel = 0;  // Reset backoff
        Serial.println("MQTT connected!");
    } else {
        mqttState = MQTT_STATE_DISCONNECTED;
        mqttBackoffLevel = min(mqttBackoffLevel + 1, 4);  // Cap at level 4 (10 minutes)
        Serial.print("MQTT connection failed, code: ");
        Serial.print(mqttClient.state());
        Serial.print(" (");
        
        // Print human-readable error
        int state = mqttClient.state();
        switch(state) {
            case -4: Serial.print("MQTT_CONNECTION_TIMEOUT"); break;
            case -3: Serial.print("MQTT_CONNECTION_LOST"); break;
            case -2: Serial.print("MQTT_CONNECT_FAILED - wrong credentials or broker rejected"); break;
            case -1: Serial.print("MQTT_DISCONNECTED"); break;
            case 0: Serial.print("MQTT_CONNECTED"); break;
            case 1: Serial.print("MQTT_CONNECT_BAD_PROTOCOL"); break;
            case 2: Serial.print("MQTT_CONNECT_BAD_CLIENT_ID"); break;
            case 3: Serial.print("MQTT_CONNECT_UNAVAILABLE"); break;
            case 4: Serial.print("MQTT_CONNECT_BAD_CREDENTIALS"); break;
            case 5: Serial.print("MQTT_CONNECT_UNAUTHORIZED"); break;
            default: Serial.print("MQTT_UNKNOWN_ERROR"); break;
        }
        Serial.println(")");
    }
}

// Internal: Handle WiFi reconnection with exponential backoff
void MqttHandler::handleWifiReconnect() {
    if (wifiState == WIFI_STATE_CONNECTING) {
        return;  // Already connecting
    }
    
    if (WiFi.status() == WL_CONNECTED) {
        return;  // Already connected
    }
    
    unsigned long now = millis();
    unsigned long backoffDelay = getBackoffDelay(wifiBackoffLevel, true);
    
    if (now - lastWifiAttempt < backoffDelay) {
        return;  // Not time to retry yet
    }
    
    Serial.print("Attempting WiFi connection (backoff level ");
    Serial.print(wifiBackoffLevel);
    Serial.println(")");
    Serial.print("  Current WiFi status: ");
    Serial.println(WiFi.status());  // 0=idle, 1=connecting, 2=wrong pass, 3=no ssid, 4=connect fail, 5=connected
    
    wifiState = WIFI_STATE_CONNECTING;
    lastWifiAttempt = now;
    
    WiFi.reconnect();
    
    wifiBackoffLevel = min(wifiBackoffLevel + 1, 4);  // Cap at level 4
}

// Internal: Calculate backoff delay based on level (with jitter)
unsigned long MqttHandler::getBackoffDelay(int backoffLevel, bool isWifi) {
    unsigned long baseDelay;
    
    if (isWifi) {
        baseDelay = WIFI_RETRY_DELAY_MS;
    } else {
        baseDelay = MQTT_RETRY_DELAY_MS;
    }
    
    // Exponential backoff: 0→base, 1→2x, 2→4x, 3→8x, 4→16x (capped at MAX)
    unsigned long delay = baseDelay;
    for (int i = 0; i < backoffLevel; i++) {
        delay *= 2;
        if (delay > MAX_BACKOFF_DELAY_MS) {
            delay = MAX_BACKOFF_DELAY_MS;
            break;
        }
    }
    
    addJitter(delay);
    return delay;
}

// Internal: Add ±10% jitter to delay
void MqttHandler::addJitter(unsigned long& delay) {
    // Generate random jitter: ±10% (multiplier from 0.9 to 1.1)
    int jitterMultiplier = random(90, 111);  // 90 to 110 (representing 0.9x to 1.1x)
    delay = (delay * jitterMultiplier) / 100;
}

// Internal: Publish diagnostic sensors
void MqttHandler::publishDiagnostics() {
    // Only publish diagnostics if MQTT is connected - don't queue them
    if (mqttState != MQTT_STATE_CONNECTED) {
        return;
    }
    
    char topic[MQTT_MAX_TOPIC_LEN];
    char payload[MQTT_MAX_PAYLOAD_LEN];
    
    // WiFi RSSI
    int rssi = WiFi.RSSI();
    if (rssi != lastWifiRssi) {
        snprintf(topic, sizeof(topic), "%s/sensor/%s/wifi_rssi/state", 
                 HA_MQTT_TOPIC_PREFIX, DEVICE_UNIQUE_ID);
        snprintf(payload, sizeof(payload), "%d", rssi);
        mqttClient.publish(topic, payload, false);
        lastWifiRssi = rssi;
    }
    
    // MQTT connection status
    snprintf(topic, sizeof(topic), "%s/sensor/%s/mqtt_connected/state", 
             HA_MQTT_TOPIC_PREFIX, DEVICE_UNIQUE_ID);
    snprintf(payload, sizeof(payload), "%s", mqttState == MQTT_STATE_CONNECTED ? "true" : "false");
    mqttClient.publish(topic, payload, false);
    
    // Uptime (in seconds)
    unsigned long uptime = getUptime();
    snprintf(topic, sizeof(topic), "%s/sensor/%s/uptime/state", 
             HA_MQTT_TOPIC_PREFIX, DEVICE_UNIQUE_ID);
    snprintf(payload, sizeof(payload), "%lu", uptime);
    mqttClient.publish(topic, payload, false);
}

// Get current MQTT connection status
MqttConnectionState MqttHandler::getConnectionStatus() {
    return mqttState;
}

// Check if MQTT is connected
bool MqttHandler::isConnected() {
    return mqttState == MQTT_STATE_CONNECTED;
}

// Get device uptime in seconds
unsigned long MqttHandler::getUptime() {
    return (millis() - deviceStartTime) / 1000;
}

#endif // MQTT_HANDLER_H
