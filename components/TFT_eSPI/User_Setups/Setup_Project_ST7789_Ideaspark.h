#pragma once

#include "sdkconfig.h"

#define USER_SETUP_INFO "Ideaspark ESP32-WROOM-32 ST7789 1.9in 170x320"

#define ST7789_DRIVER
#define TFT_WIDTH  170
#define TFT_HEIGHT 320

#define TFT_MOSI 23
#define TFT_SCLK 18
#define TFT_MISO -1
#define TFT_CS   15
#define TFT_DC   2
#define TFT_RST  4

#define TFT_BL CONFIG_USER_IDEASPARK_BACKLIGHT_GPIO
#define TFT_BACKLIGHT_ON HIGH

#define SPI_FREQUENCY 20000000
#define TFT_RGB_ORDER TFT_BGR
#define TFT_INVERSION_ON

#define LOAD_GLCD
#define TOUCH_CS -1
