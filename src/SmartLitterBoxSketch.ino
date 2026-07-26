#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <Callback.h>

#include "config.h"
#include "MqttHandler.h"
#include "SmartLitterBox.h"

// Forward declaration
void OnNewReading(CatLitterUse catLitterUse);

// MQTT handler instance
MqttHandler mqttHandler;

// Smart Litter Box instance
SmartLitterBox smartLitterBox;

void setup() 
{
    Serial.begin(9600);
    
    delay(1000);  // Give serial time to initialize
    Serial.println("\n\nSmart Litter Box starting up...");

    // Turn off the WiFi AP
    WiFi.mode(WIFI_STA);

    // Connect to WiFi and initialize MQTT handler
    Serial.print("Connecting to WiFi: ");
    Serial.println(WLAN_SSID);
    WiFi.begin(WLAN_SSID, WLAN_PASS);
    
    // Wait up to 30 seconds for initial WiFi connection
    int retries = 60;  // 30 seconds (60 * 500ms)
    while (WiFi.status() != WL_CONNECTED && retries > 0) {
        delay(500);
        Serial.print(".");
        retries--;
    }
    
    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("\nWiFi connected");
        Serial.print("IP address: ");
        Serial.println(WiFi.localIP());
    } else {
        Serial.println("\nWiFi initial connection timeout - will retry with backoff");
    }

    // Initialize MQTT handler
    mqttHandler.setup();
    
    // Attach callback for new litter box readings
    FunctionSlot<CatLitterUse> ptrSlot(OnNewReading);
    smartLitterBox.LitterUsage.attach(ptrSlot);
    
    Serial.println("Setup complete!");
}

void loop()
{
    // Tick smart litter box sensor logic
    smartLitterBox.Tick();
    
    // Tick MQTT handler (manages WiFi/MQTT reconnection, queue draining, diagnostics)
    mqttHandler.tick();
}

void OnNewReading(CatLitterUse catLitterUse)
{
    // Publish reading to Home Assistant via MQTT
    mqttHandler.publishReading(catLitterUse.CatWeight, catLitterUse.PoopWeight, catLitterUse.Duration);

    // Log reading to serial
    Serial.println("------------------------");
    Serial.print("Cat Weight = ");
    Serial.println(catLitterUse.CatWeight);

    Serial.print("Poop weight = ");
    Serial.println(catLitterUse.PoopWeight);

    Serial.print("Duration = ");
    Serial.print(catLitterUse.Duration);
    Serial.println(" ms");
    Serial.println("------------------------");
}
