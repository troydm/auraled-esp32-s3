#pragma once
#include <inttypes.h>

typedef struct crgb_t {
     uint8_t r, g, b;
} CRGB;

esp_err_t ws28xx_init(const int pin, const uint16_t led_nums, CRGB **led_buffer_ptr);
esp_err_t ws28xx_update();