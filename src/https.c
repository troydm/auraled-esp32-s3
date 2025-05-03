#include <esp_system.h>
#include <esp_https_server.h>
#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_tls.h"
#include "sdkconfig.h"
#include "config.h"

extern const char *TAG;
extern const char index_html[] asm("_binary_index_html_start");
extern const char index_html_end[] asm("_binary_index_html_end");
extern settings_t settings;

static int substrcmp(const char *str, const char *substr, const size_t len) {
    for (int i = 0; i < len; i++) {
        if (str[i] != substr[i])
        {
            return ESP_FAIL;
        }
    }
    return ESP_OK;
}

static void send_body(httpd_req_t *req)
{
    const char *form_start = strstr(index_html, "<!-- header -->") + 15;
    const char *form_end = strstr(index_html, "<!-- footer -->");
    size_t buff_len = (form_end - form_start) + 512;
    char *buffer = malloc(buff_len);

    char *start = (char *)form_start;
    int i = 0;
    char buf[64];
    while (start < form_end) {
        if (substrcmp(start, "{hostname}", 10) == ESP_OK)
        {
            strcpy(buffer + i, settings.hostname);
            i += strlen(settings.hostname);
            start += 10;
        }
        else if (substrcmp(start, "{wifiSSID}", 10) == ESP_OK)
        {
            strcpy(buffer + i, settings.wifiSSID);
            i += strlen(settings.wifiSSID);
            start += 10;
        }
        else if (substrcmp(start, "{wifiPassword}", 14) == ESP_OK)
        {
            strcpy(buffer + i, settings.wifiPassword);
            i += strlen(settings.wifiPassword);
            start += 14;
        }
        else if (substrcmp(start, "{ledPin}", 8) == ESP_OK)
        {
            itoa(settings.led_pin, buf, 10);
            strcpy(buffer + i, buf);
            i += strlen(buf);
            start += 8;
        }
        else if (substrcmp(start, "{ledNum}", 8) == ESP_OK)
        {
            itoa(settings.led_num, buf, 10);
            strcpy(buffer + i, buf);
            i += strlen(buf);
            start += 8;
        }
        else if (substrcmp(start, "{port}", 6) == ESP_OK)
        {
            itoa(settings.port, buf, 10);
            strcpy(buffer + i, buf);
            i += strlen(buf);
            start += 6;
        }
        else
        {
            buffer[i++] = *(start++);
        }
    }

    httpd_resp_send_chunk(req, buffer, i);

    free(buffer);
}

static esp_err_t header_footer_handler(httpd_req_t *req, void f(httpd_req_t*))
{
    const size_t header_len = strstr(index_html, "<!-- header -->") - index_html + 15;
    const char *footer = strstr(index_html, "<!-- footer -->");
    const size_t footer_len = index_html_end - footer;

    // set Content-Type
    httpd_resp_set_type(req, "text/html; charset=UTF-8");
    // send header
    httpd_resp_send_chunk(req, index_html, header_len);

    f(req);

    // send footer
    httpd_resp_send_chunk(req, footer, footer_len);
    httpd_resp_send_chunk(req, NULL, 0);

    return ESP_OK;
}

static esp_err_t root_get_handler(httpd_req_t *req)
{
    return header_footer_handler(req, &send_body);
}

static esp_err_t root_post_handler(httpd_req_t *req)
{
    char content[8096*4];

    size_t recv_size = req->content_len < sizeof(content) ? req->content_len : sizeof(content);

    int ret = httpd_req_recv(req, content, recv_size);
    if (ret <= 0)
    { /* 0 return value indicates connection closed */
        /* Check if timeout occurred */
        if (ret == HTTPD_SOCK_ERR_TIMEOUT)
        {
            /* In case of timeout one can choose to retry calling
             * httpd_req_recv(), but to keep it simple, here we
             * respond with an HTTP 408 (Request Timeout) error */
            httpd_resp_send_408(req);
        }
        /* In case of error, returning ESP_FAIL will
         * ensure that the underlying socket is closed */
        return ESP_FAIL;
    }
    content[recv_size] = '\0';

    read_settings_from(content);

    const esp_err_t err = header_footer_handler(req, &send_body);
    save_settings_and_reboot();

    return err;
}

static const httpd_uri_t root = {
    .uri       = "/",
    .method    = HTTP_GET,
    .handler   = root_get_handler
};

static const httpd_uri_t root_post = {
    .uri       = "/",
    .method    = HTTP_POST,
    .handler   = root_post_handler
};

static httpd_handle_t start_webserver()
{
    httpd_handle_t server = NULL;

    // Start the httpd server
    ESP_LOGI(TAG, "Starting HTTPS server");

    httpd_ssl_config_t conf = HTTPD_SSL_CONFIG_DEFAULT();

    extern const unsigned char servercert_start[] asm("_binary_servercert_pem_start");
    extern const unsigned char servercert_end[]   asm("_binary_servercert_pem_end");
    conf.servercert = servercert_start;
    conf.servercert_len = servercert_end - servercert_start;

    extern const unsigned char prvtkey_pem_start[] asm("_binary_prvtkey_pem_start");
    extern const unsigned char prvtkey_pem_end[]   asm("_binary_prvtkey_pem_end");
    conf.prvtkey_pem = prvtkey_pem_start;
    conf.prvtkey_len = prvtkey_pem_end - prvtkey_pem_start;

    esp_err_t ret = httpd_ssl_start(&server, &conf);
    if (ESP_OK != ret) {
        ESP_LOGI(TAG, "Error starting server!");
        return NULL;
    }

    // Set URI handlers
    ESP_LOGI(TAG, "Registering URI handlers");
    httpd_register_uri_handler(server, &root);
    httpd_register_uri_handler(server, &root_post);
    return server;
}

static esp_err_t stop_webserver(httpd_handle_t server)
{
    // Stop the httpd server
    return httpd_ssl_stop(server);
}

static void disconnect_handler(void* arg, esp_event_base_t event_base,
                               int32_t event_id, void* event_data)
{
    httpd_handle_t* server = (httpd_handle_t*) arg;
    if (*server) {
        if (stop_webserver(*server) == ESP_OK) {
            *server = NULL;
        } else {
            ESP_LOGE(TAG, "Failed to stop https server");
        }
    }
}

static void connect_handler(void* arg, esp_event_base_t event_base,
                            int32_t event_id, void* event_data)
{
    httpd_handle_t* server = (httpd_handle_t*) arg;
    if (*server == NULL) {
        *server = start_webserver();
    }
}

static httpd_handle_t server = NULL;

void register_sta_https_server_event_handlers()
{
    // sta events
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &connect_handler, &server));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, &disconnect_handler, &server));
    
}

void register_ap_https_server_event_handlers()
{
    // ap events 
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_AP_STACONNECTED, &connect_handler, &server));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_AP_STADISCONNECTED, &disconnect_handler, &server));
}