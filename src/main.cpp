#include <Arduino.h>
#include <Timezone.h>
#include <WiFi.h>

#include "esp_sntp.h"
#include "mqtt.h"

#define MQTT_SERVER "192.168.68.106"
#define MQTT_PORT 1883
#define MQTT_CLIENT "mqtt-test"
#define MQTT_TOPIC "mqtt-test"
#define MQTT_TEST_MESSAGE "restart"

WiFiClient wifi;
MQTT *pMQTT;

TimeChangeRule BST = {"BST", Last, Sun, Mar, 1, 60};  // British Summer Time
TimeChangeRule GMT = {"GMT", Last, Sun, Oct, 2, 0};   // Standard Time
Timezone UK(BST, GMT);
char timestamp[32];

void connectWiFi() {
  unsigned long start;
  int count, n, max_rssi, network;

  Serial.print("Scanning ... ");
  start = millis();
  count = WiFi.scanNetworks(false, false, false, 100, 0, "Wario");
  Serial.printf("%d networks found in %d ms\n", count, millis() - start);

  for (n = 0; n < count; n++) {
    Serial.printf("%d: RSSI = %d, SSID = %s, BSSID = %s, channel = %d\n", n, WiFi.RSSI(n), WiFi.SSID(n).c_str(), WiFi.BSSIDstr(n).c_str(), WiFi.channel(n));
  }

  max_rssi = -999;
  network = -1;
  for (n = 0; n < count; n++) {
    if (WiFi.RSSI(n) > max_rssi) {
      max_rssi = WiFi.RSSI(n);
      network = n;
    }
  }
  if (network == -1) {
    Serial.println("No usable network found");
    goto exit;
  }

  WiFi.mode(WIFI_STA);
  WiFi.persistent(false);
  WiFi.disconnect();

  while (WiFi.status() != WL_CONNECTED) {
    WiFi.begin("Wario", "mansion1", WiFi.channel(network), WiFi.BSSID(network));
    Serial.println("Connecting WiFi ...");

    start = millis();
    while (WiFi.status() != WL_CONNECTED) {
      if (millis() - start > 5000) {
        Serial.println("Timed out connecting to access point");
        break;
      }
      delay(100);
    }
  }

  Serial.printf("Connected as %s to access point %s\n", WiFi.localIP().toString().c_str(), WiFi.BSSIDstr().c_str());
  Serial.printf("MAC address %s\n", WiFi.macAddress().c_str());

exit:
  return;
}

bool connectMQTT() {
  bool result = false;

  delete pMQTT;
  pMQTT = new MQTT(wifi);
  pMQTT->enableDebug(true);

  if (!pMQTT->connect(MQTT_SERVER, MQTT_PORT, MQTT_CLIENT, 60)) {
    Serial.println("Error connecting MQTT");
    goto exit;
  }

  result = true;

exit:
  return result;
}

void setup() {
  Serial.begin(115200);
  Serial.println();
  Serial.println("Starting");

  connectWiFi();

  /*Serial.print("Refreshing NTP time ... ");
  sntp_setservername(0, "pool.ntp.org");
  sntp_set_sync_mode(SNTP_SYNC_MODE_IMMED);
  sntp_init();
  while (sntp_get_sync_status() != SNTP_SYNC_STATUS_COMPLETED) {
    vTaskDelay(1000);
  }
  time_t now, local;
  time(&now);
  local = UK.toLocal(now);
  tm *ptm = localtime(&local);
  sprintf(timestamp, "%02d/%02d/%04d %02d:%02d", ptm->tm_mday, ptm->tm_mon + 1, ptm->tm_year + 1900, ptm->tm_hour, ptm->tm_min);
  Serial.printf("got %s\n", timestamp);*/
  strcpy(timestamp, "unknown");

  connectMQTT();
  pMQTT->publish("mqtt-test/restart", timestamp, true);
  pMQTT->subscribe("mqtt-test/subscribe");
}

void loop() {
}