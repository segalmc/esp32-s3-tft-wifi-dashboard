#pragma once

#include <Arduino.h>

constexpr int16_t kBackgroundWidth = 320;
constexpr int16_t kBackgroundHeight = 480;
alignas(4) extern const uint16_t kBackgroundPixels[320 * 480] PROGMEM;
extern const uint32_t kBackgroundChecksum;
#if NATURE_GLASS_DIAGNOSTICS
alignas(4) extern const uint16_t kPhotoOnlyPixels[320 * 480] PROGMEM;
extern const uint32_t kPhotoOnlyChecksum;
#endif
