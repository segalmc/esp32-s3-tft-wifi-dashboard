#pragma once

namespace now_telemetry {
struct DiscoveryExchange {
    bool clear_peer;
    bool reply_sent;
    bool encrypted_peer;
    bool authorized() const { return clear_peer && reply_sent && encrypted_peer; }
};

// Discovery temporarily replaces an encrypted peer with a cleartext peer.
// Restore encryption even after a failed submission or MAC acknowledgement.
// A failed reply must never authorize a new sender session.
template<class Peer, class Send, class Wait>
DiscoveryExchange discoveryExchange(Peer peer, Send send, Wait wait) {
    const bool clear = peer(false);
    const bool sent = clear && send();
    if (sent) wait();
    const bool encrypted = peer(true);
    return {clear, sent, encrypted};
}
} // namespace now_telemetry
