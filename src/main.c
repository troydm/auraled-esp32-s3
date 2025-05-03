#include <stdio.h>
#include <inttypes.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "esp_chip_info.h"
#include "esp_wifi.h"
#include "esp_mac.h"
#include <esp_https_server.h>
#include "esp_event.h"
#include "esp_flash.h"
#include "esp_log.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include <lwip/netdb.h>
#include "driver/gpio.h"
#include "led_strip.h"
#include "ws28xx.h"
#include "https.h"
#include "config.h"

char *TAG = "auraled";
// static led_strip_handle_t led_handle;
static led_strip_handle_t status_led_handle;
static uint8_t led_initialized = 0;
static CRGB *led_buffer;

settings_t settings;

void status_led(uint8_t r, uint8_t g, uint8_t b)
{
    led_strip_set_pixel(status_led_handle, 0, r, g, b);
    led_strip_refresh(status_led_handle);
}

void status_led_init(void)
{
    led_strip_config_t strip_config = {
        .strip_gpio_num = STATUS_LED_GPIO,
        .max_leds = 1, // at least one LED on board
    };
    led_strip_rmt_config_t rmt_config = {
        .resolution_hz = 10 * 1000 * 1000, // 10MHz
        .flags.with_dma = false,
    };
    ESP_ERROR_CHECK(led_strip_new_rmt_device(&strip_config, &rmt_config, &status_led_handle));
    led_strip_clear(status_led_handle);
    status_led(1, 1, 1);
}

void led_init(void) 
{
    /*
    ESP_LOGI(TAG, "Initializing LED");
    ESP_ERROR_CHECK(ws28xx_init(LED_GPIO, WS2812B, LED_NUM, &ws2812_buffer));
    for(int i = 0; i < LED_NUM; i++) {
        ws2812_buffer[i] = (CRGB){.r=50, .g=50, .b=50};
    }
    ESP_ERROR_CHECK_WITHOUT_ABORT(ws28xx_update(ws2812_buffer));
    */
    /*
    led_strip_config_t strip_config = {
        .strip_gpio_num = LED_GPIO,
        .max_leds = LED_NUM, // at least one LED on board
    };
    led_strip_spi_config_t spi_config = {
        .spi_bus = SPI2_HOST,
        .flags.with_dma = true,
    };
    ESP_ERROR_CHECK(led_strip_new_spi_device(&strip_config, &spi_config, &led_handle));
    for(int i = 0; i < LED_NUM; i++) {
        ESP_ERROR_CHECK_WITHOUT_ABORT(led_strip_set_pixel(led_handle, i, 10, 10, 10));
    }
    ESP_ERROR_CHECK_WITHOUT_ABORT(led_strip_refresh(led_handle));
    */
   ESP_ERROR_CHECK(ws28xx_init(settings.led_pin, settings.led_num, &led_buffer));
   for(int i = 0; i < settings.led_num; i++) {
       led_buffer[i] = (CRGB){.r=10, .g=10, .b=10};
   }
   ws28xx_update();
}

static void udp_server_task(void *pvParameters)
{
    // char rx_buffer[LED_NUM * 3];
    int ip_protocol = 0;
    struct sockaddr_in dest_addr;

    ESP_LOGI(TAG, "Starting UDP server task");

    while (1) {
        struct sockaddr_in *dest_addr_ip4 = &dest_addr;
        dest_addr_ip4->sin_addr.s_addr = htonl(INADDR_ANY);
        dest_addr_ip4->sin_family = AF_INET;
        dest_addr_ip4->sin_port = htons(settings.port);
        ip_protocol = IPPROTO_IP;

        ESP_LOGI(TAG, "Creating socket");
        int sock = socket(AF_INET, SOCK_DGRAM, ip_protocol);
        if (sock < 0) {
            ESP_LOGE(TAG, "Unable to create socket: errno %d", errno);
            break;
        }
        ESP_LOGI(TAG, "Socket created");

         // Set timeout
        struct timeval timeout;
        timeout.tv_sec = 60;
        timeout.tv_usec = 0;
        setsockopt (sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof timeout);

        int err = bind(sock, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
        if (err < 0) {
            ESP_LOGE(TAG, "Socket unable to bind: errno %d", errno);
        }
        ESP_LOGI(TAG, "Socket bound, port %d", settings.port);

        struct sockaddr_storage source_addr; // Large enough for both IPv4 or IPv6
        socklen_t socklen = sizeof(source_addr);

        ESP_LOGI(TAG, "Waiting for data");
        const int udp_packet_size = settings.led_num * sizeof(CRGB);
        while (1) {
            int len = recvfrom(sock, led_buffer, udp_packet_size, 0, (struct sockaddr *)&source_addr, &socklen);

            if (len < 0)
            {
                ESP_LOGE(TAG, "recvfrom failed: errno %d", errno);
                break;
            } else if (len != udp_packet_size) {
                continue;
            }
            // Data received
            else
            {
                // ESP_LOGI(TAG, "received %d bytes", len);
                ws28xx_update();
                /*
                for (int i = 0; i < len; i += 3)
                {
                    ws2812_buffer[i/3] = (CRGB){.r = rx_buffer[i], .g = rx_buffer[i+1], .b=rx_buffer[i+2]};
                }
                ESP_ERROR_CHECK_WITHOUT_ABORT(ws28xx_update(ws2812_buffer));
                */
                // ESP_LOGI(TAG, "received %d bytes", len);
                /*
                for (int i = 0; i < len; i += 3)
                {
                    ESP_ERROR_CHECK_WITHOUT_ABORT(led_strip_set_pixel(led_handle, i / 3, rx_buffer[i], rx_buffer[i + 1], rx_buffer[i + 2]));
                }
                ESP_ERROR_CHECK_WITHOUT_ABORT(led_strip_refresh(led_handle));
                */
            }
        }

        if (sock != -1) {
            ESP_LOGE(TAG, "Shutting down socket and restarting...");
            shutdown(sock, 0);
            close(sock);
        }
    }
    vTaskDelete(NULL);
}

void print_chip_info(void) 
{
    /* Print chip information */
    esp_chip_info_t chip_info;
    uint32_t flash_size;
    esp_chip_info(&chip_info);
    printf("This is %s chip with %d CPU core(s), %s%s%s%s, ",
           CONFIG_IDF_TARGET,
           chip_info.cores,
           (chip_info.features & CHIP_FEATURE_WIFI_BGN) ? "WiFi/" : "",
           (chip_info.features & CHIP_FEATURE_BT) ? "BT" : "",
           (chip_info.features & CHIP_FEATURE_BLE) ? "BLE" : "",
           (chip_info.features & CHIP_FEATURE_IEEE802154) ? ", 802.15.4 (Zigbee/Thread)" : "");

    unsigned major_rev = chip_info.revision / 100;
    unsigned minor_rev = chip_info.revision % 100;
    printf("silicon revision v%d.%d, ", major_rev, minor_rev);
    if(esp_flash_get_size(NULL, &flash_size) != ESP_OK) {
        printf("Get flash size failed");
        return;
    }

    printf("%" PRIu32 "MB %s flash\n", flash_size / (uint32_t)(1024 * 1024),
           (chip_info.features & CHIP_FEATURE_EMB_FLASH) ? "embedded" : "external");

    printf("Minimum free heap size: %" PRIu32 " bytes\n", esp_get_minimum_free_heap_size());
    fflush(stdout);
}

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

/* FreeRTOS event group to signal when we are connected*/
static EventGroupHandle_t s_wifi_event_group;

static void event_handler(void *arg, esp_event_base_t event_base,
                          int32_t event_id, void *event_data)
{
    if (event_id == WIFI_EVENT_AP_STACONNECTED)
    {
        wifi_event_ap_staconnected_t *event = (wifi_event_ap_staconnected_t *)event_data;
        ESP_LOGI(TAG, "station "MACSTR" join, AID=%d", MAC2STR(event->mac), event->aid);
    }
    else if (event_id == WIFI_EVENT_AP_STADISCONNECTED)
    {
        wifi_event_ap_stadisconnected_t *event = (wifi_event_ap_stadisconnected_t *)event_data;
        ESP_LOGI(TAG, "station "MACSTR" leave, AID=%d, reason=%d", MAC2STR(event->mac), event->aid, event->reason);
    }
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START)
    {
        esp_wifi_connect();
    }
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED)
    {
        ESP_LOGI(TAG,"connect to the AP fail, retrying");
        xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        status_led(1, 0, 0);
        esp_wifi_connect();
    }
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
    {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
        status_led(0, 1, 0);
        if (led_initialized == 0)
        {
            // create UDP server task
            led_init();
            xTaskCreate(udp_server_task, "auraled_udp_server", 8192, NULL, 5, NULL);
            led_initialized = 1;
        }
    }
    else if (event_base == ESP_HTTPS_SERVER_EVENT)
    {
        if (event_id == HTTPS_SERVER_EVENT_ERROR)
        {
            esp_https_server_last_error_t *last_error = (esp_tls_last_error_t *)event_data;
            ESP_LOGE(TAG, "Error event triggered: last_error = %s, last_tls_err = %d, tls_flag = %d", esp_err_to_name(last_error->last_error), last_error->esp_tls_error_code, last_error->esp_tls_flags);
        }
    }
}

void wifi_init_ap(void)
{
    ESP_LOGI(TAG, "ESP_WIFI_MODE_AP");

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &event_handler,
                                                        NULL,
                                                        NULL));
    register_ap_https_server_event_handlers();
    
    wifi_config_t wifi_config = {
        .ap = {
            .ssid = ESP_AP_WIFI_SSID,
            .ssid_len = strlen(ESP_AP_WIFI_SSID),
            .channel = ESP_AP_WIFI_CHANNEL,
            .password = ESP_AP_WIFI_PASS,
            .max_connection = ESP_AP_WIFI_MAX_CONN,
            .authmode = ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD,
            .pmf_cfg = {
                    .required = true,
            },
        },
    };
    if (strlen(ESP_AP_WIFI_PASS) == 0) {
        wifi_config.ap.authmode = WIFI_AUTH_OPEN;
    }

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "wifi_init_ap finished. SSID: " ESP_AP_WIFI_SSID " password: " ESP_AP_WIFI_PASS " channel:%d", ESP_AP_WIFI_CHANNEL);
    status_led(0, 0, 1);
}

void wifi_init_sta(void)
{
    ESP_LOGI(TAG, "ESP_WIFI_MODE_STA");

    s_wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());

    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_t *netif = esp_netif_create_default_wifi_sta();

    // Set the hostname for the network interface
    esp_netif_set_hostname(netif, TAG);

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &event_handler,
                                                        NULL,
                                                        &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &event_handler,
                                                        NULL,
                                                        &instance_got_ip));
    register_sta_https_server_event_handlers();

    wifi_config_t wifi_config = {
        .sta = {
            /* Authmode threshold resets to WPA2 as default if password matches WPA2 standards (password len => 8).
             * If you want to connect the device to deprecated WEP/WPA networks, Please set the threshold value
             * to WIFI_AUTH_WEP/WIFI_AUTH_WPA_PSK and set the password with length and format matching to
             * WIFI_AUTH_WEP/WIFI_AUTH_WPA_PSK standards.
             */
            .threshold.authmode = ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD,
        },
    };

    strcpy((char *)wifi_config.sta.ssid, settings.wifiSSID);
    strcpy((char *)wifi_config.sta.password, settings.wifiPassword);

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "wifi_init_sta finished.");

    /* Waiting until either the connection is established (WIFI_CONNECTED_BIT) or connection failed. 
       The bits are set by event_handler() (see above) */
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
            WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
            pdFALSE,
            pdFALSE,
            portMAX_DELAY);

    /* xEventGroupWaitBits() returns the bits before the call returned, hence we can test which event actually
     * happened. */
    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "connected to ap SSID:%s", settings.wifiSSID);
    } else if (bits & WIFI_FAIL_BIT) {
        ESP_LOGI(TAG, "Failed to connect to SSID:%ss", settings.wifiSSID);
    } else {
        ESP_LOGE(TAG, "UNEXPECTED EVENT");
    }
}

void write_settings_to_nvs(nvs_handle_t settings_handle)
{
    nvs_set_i32(settings_handle, "led_pin", settings.led_pin);
    nvs_set_u16(settings_handle, "led_num", settings.led_num);
    nvs_set_str(settings_handle, "hostname", settings.hostname);
    nvs_set_str(settings_handle, "wifiSSID", settings.wifiSSID);
    nvs_set_str(settings_handle, "wifiPass", settings.wifiPassword);
    nvs_set_u16(settings_handle, "port", settings.port);
    ESP_ERROR_CHECK(nvs_commit(settings_handle));
}

static void urldecode(char *dst, const char *src)
{
    char a, b;
    while (*src)
    {
        if ((*src == '%') && ((a = src[1]) && (b = src[2])) && (isxdigit(a) && isxdigit(b)))
        {
            if (a >= 'a')
                a -= 'a' - 'A';
            if (a >= 'A')
                a -= ('A' - 10);
            else
                a -= '0';
            if (b >= 'a')
                b -= 'a' - 'A';
            if (b >= 'A')
                b -= ('A' - 10);
            else
                b -= '0';
            *dst++ = 16 * a + b;
            src += 3;
        }
        else if (*src == '+')
        {
            *dst++ = ' ';
            src++;
        }
        else
        {
            *dst++ = *src++;
        }
    }
    *dst++ = '\0';
}

void handle_settings_param_saving(const char* name, const char* val)
{
    char *output = malloc(strlen(val) + 1);
    urldecode(output, val);
    ESP_LOGI(TAG, "Handling URI Param %s = %s", name, val);
    if (strcmp(name, "hostname") == 0)
    {
        strcpy(settings.hostname, output);
    }
    else if (strcmp(name, "wifiSSID") == 0)
    {
        strcpy(settings.wifiSSID, output);
    }
    else if (strcmp(name, "wifiPassword") == 0)
    {
        strcpy(settings.wifiPassword, output);
    }
    else if (strcmp(name, "ledPin") == 0)
    {
        int ival = atoi(output);
        settings.led_pin = ival;
    }
    else if (strcmp(name, "ledNum") == 0)
    {
        int ival = atoi(output);
        settings.led_num = ival;
    }
    else if (strcmp(name, "port") == 0)
    {
        int ival = atoi(output);
        settings.port = ival;
    }
    else
    {
        ESP_LOGW(TAG, "Unknown param %s, ignoring", name);
    }
    free(output);
}

void read_settings_from(const char *content)
{
    char buf[512];
    const size_t len = strlen(content);
    int j = 0;
    for (int i = 0; i <= len; i++)
    {
        if (content[i] == '&' || content[i] == '\0')
        {
            if (j > 0)
            {
                buf[j] = '\0';
                int c;
                for (c = 0; c < j; c++)
                {
                    if (buf[c] == '=') {
                        break;
                    }
                }
                if (c > 0)
                {
                    buf[c] = '\0';
                    const char *uriParamName = &buf[0];
                    const char *uriParamVal = &buf[c + 1];

                    handle_settings_param_saving(uriParamName, uriParamVal);
                }

                j = 0;
            }
        }
        else
        {
            buf[j++] = content[i];
        }
    }
}

void save_settings_and_reboot()
{
    nvs_handle_t settings_handle;
    const esp_err_t ret = nvs_open("settings", NVS_READWRITE, &settings_handle);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Error (%s) opening NVS handle!\n", esp_err_to_name(ret));
    }
    else
    {
        ESP_LOGI(TAG, "Writing NVS settings and rebooting");
        write_settings_to_nvs(settings_handle);
        // Close
        nvs_close(settings_handle);

        esp_restart();
    }
}

void app_main(void)
{
    // print ESP32 chip info
    print_chip_info();

    // delay for 5 seconds just in case if something goes wrong
    vTaskDelay(5000 / portTICK_PERIOD_MS);

    //Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret != ESP_OK) {
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
      ESP_ERROR_CHECK(ret);
    }

    // Initialize settings
    int run_ap = 0;
    nvs_handle_t settings_handle;
    ret = nvs_open("settings", NVS_READWRITE, &settings_handle);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Error (%s) opening NVS handle!\n", esp_err_to_name(ret));
    }
    else
    {
        nvs_type_t key;
        if (nvs_find_key(settings_handle, "led_pin", &key) == ESP_OK)
        {
            ESP_LOGI(TAG, "Getting settings from NVS");
            nvs_get_i32(settings_handle, "led_pin", &settings.led_pin);
            nvs_get_u16(settings_handle, "led_num", &settings.led_num);
            nvs_get_u16(settings_handle, "port", &settings.port);
            size_t len = 64;
            nvs_get_str(settings_handle, "hostname", settings.hostname, &len);
            len = 64;
            nvs_get_str(settings_handle, "wifiSSID", settings.wifiSSID, &len);
            len = 64;
            nvs_get_str(settings_handle, "wifiPass", settings.wifiPassword, &len);
        }
        else
        {
            ESP_LOGI(TAG, "Initializing temporary settings for AP configuration");
            settings.led_pin = 14;
            settings.led_num = 1;
            strcpy(settings.hostname, "auraled");
            strcpy(settings.wifiSSID, "ssid");
            strcpy(settings.wifiPassword, "password");
            settings.port = 5568;
            run_ap = 1;
        }
        // Close
        nvs_close(settings_handle);

        ESP_LOGI(TAG, "Current settings:");
        ESP_LOGI(TAG, "  ledPin: %d", settings.led_pin);
        ESP_LOGI(TAG, "  ledNum: %d", settings.led_num);
        ESP_LOGI(TAG, "  hostname: %s", settings.hostname);
        ESP_LOGI(TAG, "  wifiSSID: %s", settings.wifiSSID);
        ESP_LOGI(TAG, "  wifiPassword: %s", settings.wifiPassword);
        ESP_LOGI(TAG, "  port: %d", settings.port);
    }
    TAG = (char *)settings.hostname;

    status_led_init();
    if (run_ap == 1)
    {
        wifi_init_ap();
    }
    else
    {
        wifi_init_sta();
    }

    // Handle boot button press for 3 seconds
    gpio_pullup_en(GPIO_NUM_0);
    unsigned int counter = 0;
    while (1)
    {
        if (gpio_get_level(GPIO_NUM_0) == 0) {
            counter += 200;
        } else {
            counter = 0;
        }
        if (counter >= 3000) {
            ESP_LOGI(TAG, "BOOT held for 3 seconds, erasing NVS and rebooting");
            ESP_ERROR_CHECK(nvs_flash_erase());
            counter = 0;
            esp_restart();
        }
        vTaskDelay(200 / portTICK_PERIOD_MS);
    }
}