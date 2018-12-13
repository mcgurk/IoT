/******************************************************************************
 * Copyright 2018 Google
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *****************************************************************************/
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
  /*Serial.print(DHTreadstatus);
  Serial.print(DHT.humidity, 1);
  Serial.print(",\t");
  Serial.print(DHT.temperature, 1);
  Serial.print(",\t");
  Serial.print(stop - start);
  Serial.println();*/
  //unsigned long t = time(nullptr);
  //String str = DHTreadstatus + String(DHT.temperature, 1) + ",\t" + String(DHT.humidity, 1) + ",\t" + String(stop - start);
  String str = "{ \"aikaleima\" : " + String(time(nullptr)) + ", \"lampotila\" : " + String(DHT.temperature, 1) + ", \"kosteus\" : " + String(DHT.humidity, 1) + " }";
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
