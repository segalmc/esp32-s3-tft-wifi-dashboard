#pragma once
#include "TftConfig.h"
#if TFT_HARDWARE_ENABLED
#include <WebServer.h>
#include <memory>
#include "NetworkManager.h"
#include "TuyaControl.h"

class BoilerWeb {
 public:
  void begin(NatureNetworkManager* network, TuyaControl* control);
 private:
  static void taskEntry(void* context);
  void run();
  void configure();
  bool available() const;
  bool sameOrigin();
  bool authorized();
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
  std::unique_ptr<WebServer> server_;
  bool running_ = false;
  IPAddress bound_ip_;
  char session_[65] = {};
  char csrf_[33] = {};
  char login_csrf_[33] = {};
  uint32_t session_created_ = 0;
  uint32_t session_seen_ = 0;
  uint32_t failed_since_ = 0;
  uint8_t failed_attempts_ = 0;
};
#endif
