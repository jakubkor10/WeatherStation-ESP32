#include "hardware.h"
#include "esp_log.h"
#include "driver/i2c_master.h"

static const char *TAG = "HARDWARE";

static i2c_master_bus_handle_t i2c_bus = NULL;
static bme280_handle_t bme280 = NULL;
static ssd1306_handle_t ssd1306 = NULL;

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
        ESP_LOGE(TAG, "I2C Init Failed!");
    }else{
        ESP_LOGI(TAG, "I2C Init Success");
    }
}
    
void init_bme280(){
    bme280 = bme280_create(i2c_bus, BME280_I2C_ADDRESS_DEFAULT);
    if (bme280 != NULL) {
        bme280_default_init(bme280);
        ESP_LOGI(TAG, "BME280 Init Success");
    } else {
        ESP_LOGE(TAG, "BME280 Init Failed!");
    }

}
    
void init_ssd1306(){
    i2c_master_bus_handle_t native_handle = i2c_bus_get_internal_bus_handle(i2c_bus);
    ssd1306_config_t config = {
        .display_enabled = true,
        .i2c_address = OLED_I2C_ADDRESS,
        .i2c_clock_speed = I2C_MASTER_FREQ_HZ,
    };
    
    if (ssd1306_init(native_handle, &config, &ssd1306) == ESP_OK){
        ssd1306_clear_display(ssd1306, false);
        ESP_LOGI(TAG, "SSD1306 Init Success");
    }else{
        ESP_LOGE(TAG, "SSD1306 Init Failed!");
    }
}

void print_to_ssd1306(const char text_lines[SSD1306_X1_TEXT_LINES][SSD1306_X1_TEXT_LINE_LEN]){
    for (int i = 0; i < SSD1306_X1_TEXT_LINES; i++)
    {
        ssd1306_display_text(ssd1306, i, text_lines[i], false);
    }
}

// Sensor readings access function
void read_sensor_data(sensor_data_t* data){
    bme280_read_temperature(bme280, &data->temperature);
    bme280_read_humidity(bme280, &data->humidity);
    bme280_read_pressure(bme280, &data->pressure);
}

void init_hardware(void){
    // I2C bus has to be initialized before any I2C devices
    init_i2c();

    init_bme280();
    init_ssd1306();
}
