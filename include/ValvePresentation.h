#pragma once
#include <cstdint>
namespace valve_presentation {
enum class Tone { neutral, automatic, manual, warning, fault };
struct View { const char* mode; const char* text; Tone tone; };
inline const char* text(uint8_t state, bool fresh) {
    constexpr const char* names[] = {"UNKNOWN", "WAIT", "OPENING", "CLOSING",
                                      "OPEN", "CLOSE", "FAULT"};
    return fresh && state < 7 ? names[state] : "UNKNOWN";
}
inline View view(uint8_t mode, uint8_t phase, uint8_t target, bool fresh) {
    if (!fresh || (mode != 1 && mode != 2)) return {"UNKNOWN", "UNKNOWN", Tone::neutral};
    const char* label = mode == 2 ? "MANUAL" : "AUTO";
    if (phase == 6) return {label, "FAULT", Tone::fault};
    if (phase >= 1 && phase <= 3) return {label, text(phase, true), Tone::warning};
    if ((phase == 4 && target == 1) || (phase == 5 && target == 2))
        return {label, target == 1 ? "OPEN" : "CLOSED", mode == 2 ? Tone::manual : Tone::automatic};
    return {label, "UNKNOWN", Tone::neutral};
}
}
