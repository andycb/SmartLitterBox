#include <ESP8266WiFi.h>
#include "Adafruit_MQTT.h"
#include "Adafruit_MQTT_Client.h"

#include <ESP8266WiFiMulti.h>
#include <ESP8266HTTPClient.h>
#include <ESP8266httpUpdate.h>
#include <time.h>
#include <Callback.h>

#include "SmartLitterBox.h"

/******************************** Setup *************************************/

#define WLAN_SSID       ""
#define WLAN_PASS       ""

#define AIO_SERVER      "io.adafruit.com"
#define AIO_SERVERPORT  8883
#define AIO_USERNAME    ""
#define AIO_KEY         ""

/****************************************************************************/

// WiFiFlientSecure for SSL/TLS support
WiFiClientSecure client;

// Setup the MQTT client class by passing in the WiFi client and MQTT server and login details.
Adafruit_MQTT_Client mqtt(&client, AIO_SERVER, AIO_SERVERPORT, AIO_USERNAME, AIO_KEY);

// io.adafruit.com SHA1 fingerprint
static const char *fingerprint PROGMEM = "59 3C 48 0A B1 8B 39 4E 0D 58 50 47 9A 13 55 60 CC A0 1D AF";

Adafruit_MQTT_Publish catWeightFeed = Adafruit_MQTT_Publish(&mqtt, AIO_USERNAME "/feeds/catWeight");
Adafruit_MQTT_Publish poopWeightFeed = Adafruit_MQTT_Publish(&mqtt, AIO_USERNAME "/feeds/poopWeight");
Adafruit_MQTT_Publish poopDurationFeed = Adafruit_MQTT_Publish(&mqtt, AIO_USERNAME "/feeds/poopDuration");

SmartLitterBox smartLitterBox = SmartLitterBox();

void MQTT_connect()
{
    int8_t ret;

    // Stop if already connected.
    if (mqtt.connected()) 
    {
        return;
    }

    Serial.print("Connecting to MQTT... ");

    uint8_t retries = 3;
    while ((ret = mqtt.connect()) != 0) 
    {
        Serial.println(mqtt.connectErrorString(ret));
        Serial.println("Retrying MQTT connection in 5 seconds...");
        mqtt.disconnect();

        delay(5000);  // wait 5 seconds
        retries--;
        if (retries == 0) 
        {
            // basically die and wait for WDT to reset
            while (1);
        }
  }

  Serial.println("MQTT Connected!");
}

void setup() 
{
    Serial.begin(9600);

    // Turn off the WiFi AP
    WiFi.mode(WIFI_STA);

    // Connect to wifi
    WiFi.begin(WLAN_SSID, WLAN_PASS);
    while (WiFi.status() != WL_CONNECTED)
    {
        delay(500);
        Serial.print(".");
    }

    Serial.println("WiFi connected");
    Serial.println("IP address: "); Serial.println(WiFi.localIP());

    // check the fingerprint of io.adafruit.com's SSL cert
    client.setFingerprint(fingerprint);
  
    MQTT_connect();
  
    FunctionSlot<CatLitterUse> ptrSlot(OnNewReading);
    smartLitterBox.LitterUsage.attach(ptrSlot);
}

void loop()
{
    smartLitterBox.Tick();
    MQTT_connect();
}

void OnNewReading(CatLitterUse catLitterUse)
{
    catWeightFeed.publish(catLitterUse.CatWeight);
    poopWeightFeed.publish(catLitterUse.PoopWeight);
    poopDurationFeed.publish(catLitterUse.Duration);

    Serial.println("------------------------");
    Serial.print("Cat Weight = ");
    Serial.println(catLitterUse.CatWeight);

    Serial.print("Poop weight = ");
    Serial.println(catLitterUse.PoopWeight);

    Serial.print("Duration = ");
    Serial.println(catLitterUse.Duration);
    Serial.println("------------------------");
}
