#include "../include/NowDiscoveryExchange.h"
#include <cassert>
#include <vector>

int main() {
    for (bool clear_ok : {false, true})
        for (bool send_ok : {false, true})
            for (bool encrypted_ok : {false, true}) {
                std::vector<int> calls;
                bool encrypted = true;
                const auto result = now_telemetry::discoveryExchange(
                    [&](bool secure) {
                        calls.push_back(secure ? 4 : 1);
                        if (secure ? encrypted_ok : clear_ok) encrypted = secure;
                        return secure ? encrypted_ok : clear_ok;
                    },
                    [&] { calls.push_back(2); return send_ok; },
                    [&] { calls.push_back(3); });
                assert(result.clear_peer == clear_ok);
                assert(result.reply_sent == (clear_ok && send_ok));
                assert(result.encrypted_peer == encrypted_ok);
                assert(result.authorized() == (clear_ok && send_ok && encrypted_ok));
                assert(calls.front() == 1 && calls.back() == 4);
                assert(calls.size() == (clear_ok ? (send_ok ? 4U : 3U) : 2U));
                if (encrypted_ok) assert(encrypted);
            }
    // A lost discovery reply restores the existing encrypted peer; subsequent
    // discovery may establish a session without weakening admission rules.
    unsigned authorized = 0;
    for (bool delivered : {false, false, true}) {
        bool encrypted = true;
        const auto result = now_telemetry::discoveryExchange(
            [&](bool secure) { encrypted = secure; return true; },
            [&] { return delivered; }, [] {});
        assert(encrypted);
        authorized += result.authorized();
    }
    assert(authorized == 1);
}
