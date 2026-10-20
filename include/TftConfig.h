#pragma once

// Keep display power and backlight disconnected until electrical verification.
#ifndef TFT_HARDWARE_ENABLED
#define TFT_HARDWARE_ENABLED 0
#endif
#if TFT_HARDWARE_ENABLED != 0 && TFT_HARDWARE_ENABLED != 1
#error "TFT_HARDWARE_ENABLED must be 0 or 1"
#endif

#ifndef TFT_SCLK
#define TFT_SCLK 12
#endif
#ifndef TFT_MOSI
#define TFT_MOSI 11
#endif
#ifndef TFT_CS
#define TFT_CS 10
#endif
#ifndef TFT_DC
#define TFT_DC 9
#endif
#ifndef TFT_RST
#define TFT_RST 8
#endif
#define TFT_MISO -1

#if TFT_SCLK == TFT_MOSI || TFT_SCLK == TFT_CS || TFT_SCLK == TFT_DC || TFT_SCLK == TFT_RST || TFT_MOSI == TFT_CS || TFT_MOSI == TFT_DC || TFT_MOSI == TFT_RST || TFT_CS == TFT_DC || TFT_CS == TFT_RST || TFT_DC == TFT_RST
#error "TFT SPI and control GPIOs must be distinct"
#endif

#ifndef TFT_ROTATION
#define TFT_ROTATION 0 // 0/2 portrait; 1/3 landscape
#endif
#if TFT_ROTATION < 0 || TFT_ROTATION > 3
#error "TFT_ROTATION must be 0, 1, 2, or 3"
#endif

#if TFT_HARDWARE_ENABLED
#define USER_SETUP_LOADED 1
#define ST7796_DRIVER 1
#define TFT_WIDTH 320
#define TFT_HEIGHT 480
#define LOAD_GLCD 1
#define LOAD_FONT2 1
#define SPI_FREQUENCY 10000000
#define USE_HSPI_PORT
#endif
