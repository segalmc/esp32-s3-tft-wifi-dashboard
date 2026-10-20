#pragma once

#include "TftConfig.h"

// Set this only after confirming that the board has a WS2812-compatible RGB
// LED and identifying its GPIO from the exact board schematic or vendor docs.
#ifndef RGB_LED_GPIO
#define RGB_LED_GPIO -1
#endif

#if RGB_LED_GPIO < -1 || RGB_LED_GPIO > 48
#error "RGB_LED_GPIO must be -1 (disabled) or an ESP32-S3 GPIO from 0 to 48"
#endif

#if RGB_LED_GPIO >= 0 && (RGB_LED_GPIO == TFT_SCLK || RGB_LED_GPIO == TFT_MOSI || RGB_LED_GPIO == TFT_CS || RGB_LED_GPIO == TFT_DC || RGB_LED_GPIO == TFT_RST)
#error "RGB_LED_GPIO must not overlap a TFT signal pin"
#endif

