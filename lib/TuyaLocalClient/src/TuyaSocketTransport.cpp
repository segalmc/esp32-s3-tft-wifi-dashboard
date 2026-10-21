#include "TuyaSocketTransport.h"

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>

namespace {
bool wouldBlock() { return errno == EAGAIN || errno == EWOULDBLOCK; }
}

bool TuyaSocketTransport::start(IPAddress ip, uint16_t port) {
  if (fd_ >= 0 || state_ == State::Connecting || state_ == State::Open) return false;
  fd_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (fd_ < 0) { state_ = State::Failed; return false; }
  const int flags = fcntl(fd_, F_GETFL, 0);
  if (flags < 0 || fcntl(fd_, F_SETFL, flags | O_NONBLOCK) < 0) {
    stop(); state_ = State::Failed; return false;
  }
  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_port = htons(port);
  const uint32_t host = (uint32_t(ip[0]) << 24) | (uint32_t(ip[1]) << 16) |
                        (uint32_t(ip[2]) << 8) | uint32_t(ip[3]);
  address.sin_addr.s_addr = htonl(host);
  const int result = ::connect(fd_, reinterpret_cast<sockaddr*>(&address), sizeof(address));
  if (result == 0) { state_ = State::Open; return true; }
  if (errno == EINPROGRESS || errno == EALREADY || wouldBlock()) {
    state_ = State::Connecting;
    return true;
  }
  stop(); state_ = State::Failed; return false;
}

TuyaSocketTransport::State TuyaSocketTransport::progressConnect() {
  if (state_ != State::Connecting) return state_;
  fd_set writeSet;
  FD_ZERO(&writeSet);
  FD_SET(fd_, &writeSet);
  timeval zero{};
  const int ready = select(fd_ + 1, nullptr, &writeSet, nullptr, &zero);
  if (ready == 0) return state_;
  if (ready < 0) { stop(); state_ = State::Failed; return state_; }
  int error = 0;
  socklen_t length = sizeof(error);
  if (getsockopt(fd_, SOL_SOCKET, SO_ERROR, &error, &length) < 0 || error) {
    stop(); state_ = State::Failed; return state_;
  }
  state_ = State::Open;
  return state_;
}

size_t TuyaSocketTransport::write(const uint8_t* data, size_t size) {
  if (state_ != State::Open || !data || !size || txPending() || size > kTxCapacity)
    return 0;
  memcpy(tx_, data, size);
  txLength_ = size;
  txOffset_ = 0;
  return size;
}

int TuyaSocketTransport::serviceTx(size_t byteBudget) {
  if (state_ != State::Open || !txPending() || !byteBudget) return 0;
  size_t length = txLength_ - txOffset_;
  if (length > byteBudget) length = byteBudget;
  const int written = ::send(fd_, tx_ + txOffset_, length, MSG_DONTWAIT);
  if (written > 0) {
    txOffset_ += static_cast<size_t>(written);
    if (!txPending()) txLength_ = txOffset_ = 0;
    return written;
  }
  if (written < 0 && wouldBlock()) return 0;
  stop(); state_ = State::Failed; return -1;
}

uint8_t TuyaSocketTransport::connected() {
  if (state_ != State::Open) return 0;
  uint8_t one;
  const int result = ::recv(fd_, &one, 1, MSG_PEEK | MSG_DONTWAIT);
  if (result > 0 || (result < 0 && wouldBlock())) return 1;
  stop(); state_ = State::Failed; return 0;
}

int TuyaSocketTransport::available() {
  if (!connected()) return 0;
  int count = 0;
  if (ioctl(fd_, FIONREAD, &count) == 0) return count;
  stop(); state_ = State::Failed; return 0;
}

int TuyaSocketTransport::read(uint8_t* data, size_t size) {
  if (state_ != State::Open || !data || !size) return -1;
  const int got = ::recv(fd_, data, size, MSG_DONTWAIT);
  if (got > 0) return got;
  if (got < 0 && wouldBlock()) return 0;
  stop(); state_ = State::Failed; return -1;
}

int TuyaSocketTransport::read() {
  uint8_t one = 0;
  return read(&one, 1) == 1 ? one : -1;
}

int TuyaSocketTransport::peek() {
  if (state_ != State::Open) return -1;
  uint8_t one = 0;
  const int got = ::recv(fd_, &one, 1, MSG_PEEK | MSG_DONTWAIT);
  if (got == 1) return one;
  if (got == 0 || (got < 0 && !wouldBlock())) {
    stop(); state_ = State::Failed;
  }
  return -1;
}

void TuyaSocketTransport::stop() {
  if (fd_ >= 0) ::close(fd_);
  fd_ = -1;
  txLength_ = txOffset_ = 0;
  memset(tx_, 0, sizeof(tx_));
  state_ = State::Closed;
}
