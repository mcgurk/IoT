// Google Cloud IoT JWT
// Quickstart:
// https://github.com/GoogleCloudPlatform/google-cloud-iot-arduino
//
// File -> Examples -> Google Cloud IoT JWT -> Esp8266-lwmqtt
// Remember to edit ciotc_config.h
//
// Public key format: ES256
// Key creation:
// openssl ecparam -genkey -name prime256v1 -noout -out ec_private.pem
// openssl ec -in ec_private.pem -pubout -out ec_public.pem
// cat ec_public.pem
//

#include "esp8266_mqtt.h"

#include <dht.h>
dht DHT;
#define DHT22_PIN D5

struct
{
    uint32_t total;
    uint32_t ok;
    uint32_t crc_error;
    uint32_t time_out;
    uint32_t connect;
    uint32_t ack_l;
    uint32_t ack_h;
    uint32_t unknown;
} stat = { 0,0,0,0,0,0,0,0};

String getTempSensor() {
  uint32_t start = micros();
  int chk = DHT.read22(DHT22_PIN);
  uint32_t stop = micros();
  String DHTreadstatus;
  
  switch (chk) {
    case DHTLIB_OK:
        stat.ok++;
        DHTreadstatus = "OK,\t";
        break;
    case DHTLIB_ERROR_CHECKSUM:
        stat.crc_error++;
        DHTreadstatus = "Checksum error,\t";
        break;
    case DHTLIB_ERROR_TIMEOUT:
        stat.time_out++;
        DHTreadstatus = "Time out error,\t";
        break;
    default:
        stat.unknown++;
        DHTreadstatus = "Unknown error,\t";
        break;
  }
  // DISPLAY DATA
  //String str = DHTreadstatus + String(DHT.temperature, 1) + ",\t" + String(DHT.humidity, 1) + ",\t" + String(stop - start);
  String str = "{ \"aikaleima\" : " + String(time(nullptr)) + ", \"lampotila\" : " + String(DHT.temperature, 1) + ", \"kosteus\" : " + String(DHT.humidity, 1) + " }";
  // { "aikaleima" : 1544730930, "lampotila" : 24.3, "kosteus" : 20.9 }
  Serial.println(str);
  //return  DHTreadstatus + String(DHT.humidity) + ",\t" + String(DHT.temperature) + ",\t" + String(stop - start);
  return str;
}

void setup() {
  // put your setup code here, to run once:
  Serial.begin(115200);
  setupCloudIoT(); // Creates globals for MQTT
  pinMode(LED_BUILTIN, OUTPUT);
  startMQTT();
  
  Serial.print("DHT LIBRARY VERSION: ");
  Serial.println(DHT_LIB_VERSION);
}

unsigned long lastMillis = 0;

void loop() {
  mqttClient->loop();
  delay(10);  // <- fixes some issues with WiFi stability

  if (!mqttClient->connected()) {
    connect();
  }

  // publish a message roughly every 30 second.
  if (millis() - lastMillis > 30000) {
  //if (millis() - lastMillis > 2000) {
    lastMillis = millis();
    publishTelemetry(getTempSensor());
  }
}
