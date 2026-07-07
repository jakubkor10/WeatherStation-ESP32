/**
 * IoT Weather Station on ESP32 (FreeRTOS + InfluxDB)
 * Program reads environmental data from a BME280 sensor using an I2C bus,
 * displays it on an OLED SSD1306 screen,
 * and sends it asynchronously to an InfluxDB database through Wi-Fi.
 */

#include <stdio.h>

#include "config.h"
#include "network.h"
#include "hardware.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"

#include "esp_log.h"

#include "nvs_flash.h"

#define TAG "MAIN"

static QueueHandle_t sensor_queue = NULL;

void init_nvs(){
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    if (CONFIG_LOG_MAXIMUM_LEVEL > CONFIG_LOG_DEFAULT_LEVEL) {
        esp_log_level_set("wifi", CONFIG_LOG_MAXIMUM_LEVEL);
    }
}

void influxdb_task(void *pvParameters){
    // initialize wifi and http client
    init_network();

    while(1){
        sensor_data_t data;
        if (xQueueReceive(sensor_queue, &data, pdMS_TO_TICKS(MAX_QUEUE_WAIT_MS)) == pdPASS){
            if (is_wifi_connected()){
                send_http_request(&data);
            }
            else{
                ESP_LOGI(TAG, "COULDN'T SEND HTTP REQUEST: NO CONNECTION");
            }
        }
    }
}

void app_main(void)
{
    // initialize storage
    init_nvs();

    // initialize i2c and sensors
    init_hardware();

    // ssd1306 screen content
    char content[SSD1306_X1_TEXT_LINES][SSD1306_X1_TEXT_LINE_LEN] = 
        {"WELCOME!","Initialization","in progress:","",};
    print_to_ssd1306(content);
    
    // queue for sensor measurements
    sensor_queue = xQueueCreate(MEASUREMENTS_QUEUE_LEN, sizeof(sensor_data_t));

    // creating an asynchronous task for sending data to database
    xTaskCreatePinnedToCore(influxdb_task, "influx_task", 4096, NULL, 5, NULL, 1);

    while(1){
        sensor_data_t sensor_data = {};

        read_sensor_data(&sensor_data);

        ESP_LOGI(TAG, "Temp: %f | Hum: %f | Press: %f", 
                sensor_data.temperature, sensor_data.humidity, sensor_data.pressure);

        snprintf(content[0], SSD1306_X1_TEXT_LINE_LEN, "Measurements:  ");
        snprintf(content[1], SSD1306_X1_TEXT_LINE_LEN, "T: %4.2f\xB0""C     ", sensor_data.temperature);
        snprintf(content[2], SSD1306_X1_TEXT_LINE_LEN, "H: %5.2f%%     ", sensor_data.humidity);
        snprintf(content[3], SSD1306_X1_TEXT_LINE_LEN, "P: %6.2fhPa   ", sensor_data.pressure);
        
        print_to_ssd1306(content);

        xQueueSend(sensor_queue, &sensor_data, pdMS_TO_TICKS(MAX_QUEUE_WAIT_MS));

        vTaskDelay(pdMS_TO_TICKS(MEASUREMENT_DELAY_MS));
    }
}