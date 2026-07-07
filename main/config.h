#pragma once
#ifndef CONFIG_H
#define CONFIG_H

// Hardware config
#define I2C_MASTER_SDA_IO (GPIO_NUM_17)
#define I2C_MASTER_SCL_IO (GPIO_NUM_16)
#define I2C_MASTER_FREQ_HZ (100000) // 100kHZ
#define I2C_MASTER_NUM (I2C_NUM_0)

#define OLED_I2C_ADDRESS 0x3C
#define OLED_PIXEL_CLOCK_HZ (400 * 1000)

#define SSD1306_X1_TEXT_LINES (4)
#define SSD1306_X1_TEXT_LINE_LEN (20) // line len is 16 but we add padding

// Network config
#define ESP_MAXIMUM_RETRY (20)

#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA2_PSK
#define ESP_WIFI_SAE_MODE WPA3_SAE_PWE_BOTH
#define EXAMPLE_H2E_IDENTIFIER ""

#define REQUEST_BUFFER_SIZE (96) // rounded up to a multiple of 32

// Main / Queue config
#define MEASUREMENTS_QUEUE_LEN (5)
#define MAX_QUEUE_WAIT_MS (1000)
#define MEASUREMENT_DELAY_MS (5*1000)

typedef struct{
    float temperature;
    float humidity;
    float pressure;
} sensor_data_t;

#endif // CONFIG_H