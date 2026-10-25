#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <LynkTuya.h>
#include <cstddef>
#include <cstdint>

// One IPv4 TCP socket. No DNS, select wait, heap allocation, or internal task.
// All methods are called by the same owner as TuyaLocalClient.
class TuyaSocketTransport : public Client {
 public:
  enum class State : uint8_t { Closed, Connecting, Open, Failed };
  TuyaSocketTransport() = default;
  ~TuyaSocketTransport() override { stop(); }
  TuyaSocketTransport(const TuyaSocketTransport&) = delete;
  TuyaSocketTransport& operator=(const TuyaSocketTransport&) = delete;

  virtual bool start(IPAddress ip, uint16_t port);
  virtual State progressConnect();
  virtual int serviceTx(size_t byteBudget);  // bytes sent, or -1 on fatal error
  virtual bool txPending() const { return txOffset_ < txLength_; }
  virtual State state() const { return state_; }
  int nativeFd() const { return fd_; }  // diagnostic/test only; never exposed by Web

  int connect(IPAddress ip, uint16_t port) override { return start(ip, port) ? 1 : 0; }
  int connect(const char*, uint16_t) { return 0; }  // IPv4 is required by config.
  size_t write(uint8_t value) { return write(&value, 1); }
  size_t write(const uint8_t* data, size_t size) override;
  int available() override;
  int read();
  int read(uint8_t* data, size_t size) override;
  int peek();
  void flush() {}  // Sending is driven by serviceTx().
  void stop() override;
  uint8_t connected() override;
  operator bool() { return connected() != 0; }

 private:
  static constexpr size_t kTxCapacity = PAYLOAD_BUFFER_SIZE + 52;
  int fd_ = -1;
  State state_ = State::Closed;
  uint8_t tx_[kTxCapacity] = {};
  size_t txLength_ = 0;
  size_t txOffset_ = 0;
};
