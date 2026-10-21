#include <WiFi.h>
#include <TuyaLocalClient.h>
#include "local_secrets.h"  // Supply privately; never commit real values.

TuyaLocalClient tuya;

void statusReceived(void*, const String& json) {
  // Parse the DPS you need here. Do not print decrypted device data.
  (void)json;
}

void setup() {
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  TuyaLocalConfig config;
  config.host.fromString(TUYA_IP);
  config.deviceId = TUYA_ID;
  config.localKey = TUYA_KEY;
  TuyaLocalClient::Callbacks callbacks;
  callbacks.onStatus = statusReceived;
  callbacks.onPush = statusReceived;
  tuya.begin(config, callbacks);  // No connection until poll() is called.
}

void loop() {
  tuya.poll(WiFi.status() == WL_CONNECTED);
  delay(25);
}
