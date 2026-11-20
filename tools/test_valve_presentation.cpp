#include "ValvePresentation.h"
#include <cassert>
#include <initializer_list>
#include <cstring>
int main() {
    assert(!strcmp(valve_presentation::text(4, true), "OPEN"));
    assert(!strcmp(valve_presentation::text(5, true), "CLOSE"));
    assert(!strcmp(valve_presentation::text(6, true), "FAULT"));
    for (unsigned i = 0; i < 256; ++i) {
        assert(!strcmp(valve_presentation::text(i, false), "UNKNOWN"));
        if (i >= 7) assert(!strcmp(valve_presentation::text(i, true), "UNKNOWN"));
    }
    using namespace valve_presentation;
    for (uint8_t mode : {1, 2}) {
        for (uint8_t target : {1, 2}) {
            auto v = view(mode, target == 1 ? 4 : 5, target, true);
            assert(v.tone == (mode == 1 ? Tone::automatic : Tone::manual));
            assert(!strcmp(v.mode, mode == 1 ? "AUTO" : "MANUAL"));
            assert(!strcmp(v.text, target == 1 ? "OPEN" : "CLOSED"));
            assert(view(mode, 6, target, true).tone == Tone::fault);
            for (uint8_t phase : {1, 2, 3}) assert(view(mode, phase, target, true).tone == Tone::warning);
            assert(view(mode, 4, target, false).tone == Tone::neutral);
        }
    }
    assert(view(0, 4, 1, true).tone == Tone::neutral);
    assert(view(1, 4, 2, true).tone == Tone::neutral);
    // Reconnection uses the new authoritative mode, never the previous view.
    assert(!strcmp(view(2, 5, 2, false).mode, "UNKNOWN"));
    assert(!strcmp(view(1, 5, 2, true).mode, "AUTO"));
}
