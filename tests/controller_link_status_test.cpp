#include "../include/ControllerLinkStatus.h"
#include <cassert>
using namespace controller_link;
int main() {
    assert(state(false, 0, 0, 0, 0, 0, 1000) == State::Connecting);
    assert(state(false, 0, 0, 0, 0, 0, 36000) == State::Offline);
    assert(state(true, 1, 1, 1000, 1000, UINT32_MAX, 36000) == State::EspNow);
    assert(state(true, 2, 2, 1000, UINT32_MAX, 1000, 36000) == State::HttpFallback);
    // A direct recovery probe does not claim ESP-NOW is primary yet.
    assert(state(true, 1, 2, 1000, 1000, 2000, 36000) == State::HttpFallback);
    assert(state(true, 1, 2, 1000, 1000, 36000, 36000) == State::Connecting);
    assert(state(true, 1, 1, 36000, 36000, UINT32_MAX, 36000) == State::Offline);
    assert(state(true, 0, 0, 100, UINT32_MAX, UINT32_MAX, 36000) == State::Connecting);
}
