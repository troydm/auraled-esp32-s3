#pragma once
#include "nvs_flash.h"

#define STATUS_LED_GPIO 48
#define ESP_AP_WIFI_SSID "auraled"
#define ESP_AP_WIFI_PASS "aura12345"
#define ESP_AP_WIFI_CHANNEL 6
#define ESP_AP_WIFI_MAX_CONN 3
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA2_PSK

typedef struct {
    int led_pin;
    uint16_t led_num;
    char hostname[64];
    char wifiSSID[64];
    char wifiPassword[64];
    uint16_t port;
} settings_t;

void save_settings_and_reboot();
void read_settings_from(const char *content);
void write_settings_to_nvs(nvs_handle_t settings_handle);
