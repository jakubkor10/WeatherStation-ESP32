/**
 * @file main.c
 * @author jakubkor10
 * @brief IoT Weather Station on ESP32 (FreeRTOS + InfluxDB)
 * * Program reads environmental data from a BME280 sensor using a I2C bus,
 * displays it on an OLED SSD1306 screen,
 * and sends it asynchronously to a InfluxDB database through Wi-Fi.
 */

#include <stdio.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"

#include "bme280.h"
#include "driver/i2c_master.h"

#include "ssd1306.h"

#include "esp_http_client.h"
#include "esp_wifi.h"

#include "nvs_flash.h"

#define I2C_MASTER_SDA_IO (GPIO_NUM_17)
#define I2C_MASTER_SCL_IO (GPIO_NUM_16)
#define I2C_MASTER_FREQ_HZ (100000) // 100kHZ
#define I2C_MASTER_NUM (I2C_NUM_0)

#define OLED_I2C_ADDRESS 0x3C
#define OLED_PIXEL_CLOCK_HZ (400 * 1000)

// TAG FOR LOGS
#define TAG "ESP32"

// SENSORS AND I2C
static i2c_master_bus_handle_t i2c_bus = NULL;
static bme280_handle_t bme280 = NULL;
static ssd1306_handle_t ssd1306 = NULL;

typedef struct{
    float temperature;
    float humidity;
    float pressure;
} sensor_data_t;

// WIFI
// 
#include "secrets.h"
#define ESP_WIFI_SSID WIFI_SSID
#define ESP_WIFI_PASSWORD WIFI_PASSWORD

// HTTP
static esp_http_client_handle_t http_client = NULL;
#define POST_URL DB_URL
#define DB_TOKEN INFLUXDB_TOKEN

void init_i2c(){
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = I2C_MASTER_SDA_IO,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_io_num = I2C_MASTER_SCL_IO,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = I2C_MASTER_FREQ_HZ,
    };
    i2c_bus = i2c_bus_create(I2C_NUM_0, &conf);
    
    if (i2c_bus == NULL) {
        ESP_LOGE(TAG, "Błąd inicjalizacji magistrali I2C!");
        return;
    }
}
    
void init_bme280(){
    bme280 = bme280_create(i2c_bus, BME280_I2C_ADDRESS_DEFAULT);
    if (bme280 != NULL) {
        bme280_default_init(bme280);
        ESP_LOGI(TAG, "BME280 pomyślnie uruchomiony.");
    } else {
        ESP_LOGE(TAG, "Nie znaleziono czujnika BME280!");
    }

}
    
void init_ssd1306(){
    i2c_master_bus_handle_t native_handle = i2c_bus_get_internal_bus_handle(i2c_bus);
    ssd1306_config_t config = {
        .display_enabled = true,
        .i2c_address = OLED_I2C_ADDRESS,
        .i2c_clock_speed = I2C_MASTER_FREQ_HZ,
    };
    
    ESP_ERROR_CHECK(ssd1306_init(native_handle, &config, &ssd1306));
    
    ssd1306_clear_display(ssd1306, false);

}

void init_http_client(){
    esp_http_client_config_t conf = {
        .url = POST_URL,
        .method = HTTP_METHOD_POST,
    };
    
    http_client = esp_http_client_init(&conf);
    
    if (http_client == NULL){
        ESP_LOGI(TAG, "ERROR: HTTP CLIENT INIT FAILED");
        return;
    }

    esp_http_client_set_header(http_client, "Authorization", INFLUXDB_TOKEN);
    esp_http_client_set_header(http_client, "Content-Type", "text/plain; charset=utf-8");

    ESP_LOGI(TAG, "HTTP CLIENT INIT SUCCESS");
}

/* FreeRTOS event group to signal when we are connected*/
static EventGroupHandle_t s_wifi_event_group;

/* The event group allows multiple bits for each event, but we only care about two events:
 * - we are connected to the AP with an IP
 * - we failed to connect after the maximum amount of retries */
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

static int s_retry_num = 0;

#define ESP_MAXIMUM_RETRY (20)

static void event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retry_num < ESP_MAXIMUM_RETRY) {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGI(TAG, "retry to connect to the AP");
        } else {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
        ESP_LOGI(TAG,"connect to the AP fail");
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA2_PSK
#define ESP_WIFI_SAE_MODE WPA3_SAE_PWE_BOTH
#define EXAMPLE_H2E_IDENTIFIER ""

void wifi_init_sta(){
    s_wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());

    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

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

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = ESP_WIFI_SSID,
            .password = ESP_WIFI_PASSWORD,
            .threshold.authmode = ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD,
            .sae_pwe_h2e = ESP_WIFI_SAE_MODE,
            .sae_h2e_identifier = EXAMPLE_H2E_IDENTIFIER,
#ifdef CONFIG_ESP_WIFI_WPA3_COMPATIBLE_SUPPORT
            .disable_wpa3_compatible_mode = 0,
#endif
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA) );
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config) );
    ESP_ERROR_CHECK(esp_wifi_start() );

    ESP_LOGI(TAG, "wifi_init_sta finished.");

    /* Waiting until either the connection is established (WIFI_CONNECTED_BIT) or connection failed for the maximum
     * number of re-tries (WIFI_FAIL_BIT). The bits are set by event_handler() (see above) */
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
            WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
            pdFALSE,
            pdFALSE,
            portMAX_DELAY);

    /* xEventGroupWaitBits() returns the bits before the call returned, hence we can test which event actually
     * happened. */
    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "connected to ap SSID:%s password:%s",
                 ESP_WIFI_SSID, ESP_WIFI_PASSWORD);
    } else if (bits & WIFI_FAIL_BIT) {
        ESP_LOGI(TAG, "Failed to connect to SSID:%s, password:%s",
                 ESP_WIFI_SSID, ESP_WIFI_PASSWORD);
    } else {
        ESP_LOGE(TAG, "UNEXPECTED EVENT");
    }   
}
#define MEASUREMENT_DELAY_MS (5*1000)

void init_nvs(){
     //Initialize NVS
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

#define SSD1306_X1_TEXT_LINES (4)
#define SSD1306_X1_TEXT_LINE_LEN (20)
void print_to_ssd1306(const char text_lines[SSD1306_X1_TEXT_LINES][SSD1306_X1_TEXT_LINE_LEN]){
    for (int i = 0; i < SSD1306_X1_TEXT_LINES; i++)
    {
        ssd1306_display_text(ssd1306, i, text_lines[i], false);
    }
}

#define REQUEST_BUFFER_SIZE (96)

void send_http_request(sensor_data_t* data){
    char post_data_buffer[REQUEST_BUFFER_SIZE];
    snprintf(post_data_buffer, REQUEST_BUFFER_SIZE, 
            "weather,location=home,sensor=bme280 temperature=%4.2f,humidity=%5.2f,pressure=%6.2f", 
            data->temperature, data->humidity, data->pressure);
    
    ESP_ERROR_CHECK(esp_http_client_set_post_field(http_client, post_data_buffer, strlen(post_data_buffer)));

    esp_err_t err = esp_http_client_perform(http_client);

    if (err == ESP_OK) {
        ESP_LOGI(TAG, "HTTP STATUS: %d", esp_http_client_get_status_code(http_client));
    } else {
        ESP_LOGE(TAG, "HTTP SEND ERROR: %s", esp_err_to_name(err));
    }
}

static QueueHandle_t sensor_queue = NULL;
#define MEASUREMENTS_QUEUE_LEN (5)

#define MAX_QUEUE_WAIT_MS (1000)

void influxdb_task(void *pvParameters){
    while(1){
        sensor_data_t data;
        if (xQueueReceive(sensor_queue, &data, pdMS_TO_TICKS(MAX_QUEUE_WAIT_MS)) == pdPASS){
            if (http_client != NULL && (xEventGroupGetBits(s_wifi_event_group) & WIFI_CONNECTED_BIT)){
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
    init_i2c();
    init_ssd1306();

    // ssd1306 screen content
    char content[SSD1306_X1_TEXT_LINES][SSD1306_X1_TEXT_LINE_LEN] = 
        {"Welcome!","T:","H:","P:",};
    print_to_ssd1306(content);

    init_bme280();

    // initialize wifi and http client
    ESP_LOGI(TAG, "ESP_WIFI_MODE_STA");
    wifi_init_sta();
    init_http_client();
    
    sensor_queue = xQueueCreate(MEASUREMENTS_QUEUE_LEN, sizeof(sensor_data_t));

    // creating an asynchronous task for sending data to database
    xTaskCreatePinnedToCore(influxdb_task, "influx_task", 4096, NULL, 5, NULL, 1);


    while(1){
        sensor_data_t sensor_data = {};

        bme280_read_temperature(bme280, &sensor_data.temperature);
        bme280_read_humidity(bme280, &sensor_data.humidity);
        bme280_read_pressure(bme280, &sensor_data.pressure);

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