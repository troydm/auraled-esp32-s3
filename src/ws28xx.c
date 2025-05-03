#include <stdio.h>
#include <string.h>
#include "esp_system.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "ws28xx.h"

static CRGB *led_buffer;
static uint16_t n_of_leds;
static size_t dma_buf_size, reset_delay;
static uint16_t *dma_buffer;
static spi_device_handle_t spi;

esp_err_t ws28xx_init(const int pin, const uint16_t led_nums, CRGB **led_buffer_ptr)
{
    esp_err_t err = ESP_OK;
    n_of_leds = led_nums;
    const spi_host_device_t spi_host = SPI2_HOST;
    reset_delay = 12;
    dma_buf_size = (led_nums * (sizeof(CRGB) * sizeof(uint16_t) * 2)) + (reset_delay + 1) * sizeof(uint16_t);

    led_buffer = malloc(sizeof(CRGB) * led_nums);
    if (led_buffer == NULL)
    {
        return ESP_ERR_NO_MEM;
    }
    *led_buffer_ptr = led_buffer;

    const spi_bus_config_t spi_bus_config = {
        .mosi_io_num = pin,
        .max_transfer_sz = dma_buf_size,
        .miso_io_num = -1,
        .sclk_io_num = -1,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
    };
    err = spi_bus_initialize(spi_host, &spi_bus_config, SPI_DMA_CH_AUTO);
    if (err != ESP_OK)
    {
        free(led_buffer);
        return err;
    }

    const spi_device_interface_config_t spi_dev_cfg = {
        .clock_speed_hz = 3.2 * 1000 * 1000, // Clock out at 3.2 MHz
        .mode = 0,                           // SPI mode 0
        .spics_io_num = -1,                  // CS pin
        .queue_size = 1,
        .command_bits = 0,
        .address_bits = 0,
        .flags = SPI_DEVICE_TXBIT_LSBFIRST,
    };
    err = spi_bus_add_device(spi_host, &spi_dev_cfg, &spi);
    if (err != ESP_OK)
    {
        free(led_buffer);
        return err;
    }
    // Critical to be DMA memory.
    dma_buffer = heap_caps_malloc(dma_buf_size, MALLOC_CAP_DMA);
    if (dma_buffer == NULL)
    {
        free(led_buffer);
        return ESP_ERR_NO_MEM;
    }

    return err;
}

static const uint16_t timing_bits[16] = {
    0x1111, 0x7111, 0x1711, 0x7711, 0x1171, 0x7171, 0x1771, 0x7771,
    0x1117, 0x7117, 0x1717, 0x7717, 0x1177, 0x7177, 0x1777, 0x7777};

esp_err_t ws28xx_update()
{
    memset(dma_buffer, 0, dma_buf_size);

    int n = 1;
    //dma_buffer[n++] = 0;

    for (int i = 0; i < n_of_leds; i++)
    {
        // Data you want to write to each LEDs
        const CRGB led = led_buffer[i];

        // Green
        dma_buffer[n++] = timing_bits[0x0f & (led.g)];
        dma_buffer[n++] = timing_bits[0x0f & (led.g >> 4)];

        // Red
        dma_buffer[n++] = timing_bits[0x0f & (led.r)];
        dma_buffer[n++] = timing_bits[0x0f & (led.r >> 4)];

        // Blue
        dma_buffer[n++] = timing_bits[0x0f & (led.b)];
        dma_buffer[n++] = timing_bits[0x0f & (led.b >> 4)];
    }

    /*
    for (int i = 0; i < reset_delay; i++) {
        dma_buffer[n++] = 0;
    }
    */

    return spi_device_transmit(spi, &(spi_transaction_t){
                                        .length = dma_buf_size * 8,
                                        .tx_buffer = dma_buffer,
                                    });
}