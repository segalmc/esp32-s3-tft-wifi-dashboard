#pragma once
#include "TftConfig.h"
#if TFT_HARDWARE_ENABLED
#include <WebServer.h>
#include <memory>
#include "BoilerWebSessions.h"
#include "NetworkManager.h"
#include "TuyaControl.h"
class LocalHttpTelemetry;

class BoilerWeb {
 public:
  void begin(NatureNetworkManager* network, TuyaControl* control,
             LocalHttpTelemetry* telemetry);
 private:
  static void taskEntry(void* context);
  void run();
  void configure();
  bool available() const;
  bool sameOrigin();
  bool authorized();
  boiler_web_sessions::Session* currentSession();
  bool csrfValid();
  void headers();
  void reject(int code, const char* message);
  void root();
  void login();
  void logout();
  void status();
  void command();
  static void randomHex(char* output, size_t bytes);
  NatureNetworkManager* network_ = nullptr;
  TuyaControl* control_ = nullptr;
  LocalHttpTelemetry* telemetry_ = nullptr;
  std::unique_ptr<WebServer> server_;
  bool running_ = false;
  IPAddress bound_ip_;
  boiler_web_sessions::Table sessions_;
  char login_csrf_[33] = {};
  uint32_t failed_since_ = 0;
  uint8_t failed_attempts_ = 0;
};
#endif
