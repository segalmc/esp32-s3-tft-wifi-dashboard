#pragma once

namespace ota_policy {
// ArduinoOTA listens on all local interfaces, so never service it while the
// setup AP is active, even when the STA side is online.
inline bool mayServe(bool online, bool ap_active, bool linked, bool password_ready) {
  return online && !ap_active && linked && password_ready;
}
}  // namespace ota_policy
