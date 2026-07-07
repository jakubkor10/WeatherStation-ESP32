#include "network.h"
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_http_client.h"

#include "secrets.h"

#define ESP_WIFI_SSID WIFI_SSID
#define ESP_WIFI_PASSWORD WIFI_PASSWORD

static const char *TAG = "NETWORK";

static esp_http_client_handle_t http_client = NULL;
static EventGroupHandle_t s_wifi_event_group;
static int s_retry_num = 0;

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

#define POST_URL DB_URL
#define DB_TOKEN INFLUXDB_TOKEN

void init_http_client(){
    esp_http_client_config_t conf = {
        .url = POST_URL,
        .method = HTTP_METHOD_POST,
    };
    
    http_client = esp_http_client_init(&conf);
    
    if (http_client == NULL){
        ESP_LOGE(TAG, "HTTP Client Init Failed!");
        return;
    }

    esp_http_client_set_header(http_client, "Authorization", INFLUXDB_TOKEN);
    esp_http_client_set_header(http_client, "Content-Type", "text/plain; charset=utf-8");

    ESP_LOGI(TAG, "HTTP Client Init Success");
}

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

bool is_wifi_connected(){
    return http_client != NULL && (xEventGroupGetBits(s_wifi_event_group) & WIFI_CONNECTED_BIT);
}

void init_network(){
    wifi_init_sta();
    init_http_client();
}