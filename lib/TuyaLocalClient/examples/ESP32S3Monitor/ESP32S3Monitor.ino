#include <WiFi.h>
#include <TuyaLocalClient.h>
#include "local_secrets.h"  // Private, ignored local configuration.

TuyaLocalClient tuya;

void mappedState(void*, uint16_t, bool on, bool) {
  // A protocol-authenticated DPS value; apply application freshness/safety rules.
  Serial.println(on ? "Mapped DPS is ON" : "Mapped DPS is OFF");
}

void setup() {
  Serial.begin(115200);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  TuyaLocalConfig config;
  config.host.fromString(TUYA_IP);
  config.deviceId = TUYA_ID;
  config.localKey = TUYA_KEY;
  config.dps.stateBoolean = 1;
  TuyaLocalClient::Callbacks callbacks;
  callbacks.onMappedBoolean = mappedState;
  if (!tuya.begin(config, callbacks)) Serial.println("Invalid Tuya configuration");
}

void loop() {
  tuya.poll(WiFi.status() == WL_CONNECTED);
  delay(25);
}
