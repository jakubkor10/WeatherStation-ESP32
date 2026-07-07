#pragma once

#include "config.h"
#include "bme280.h"
#include "ssd1306.h"

// Initialization of all hardware (I2C, BME280, OLED)
void init_hardware(void);

// Sensor readings access function
void read_sensor_data(sensor_data_t* data);

// Function for screen printing
void print_to_ssd1306(const char text_lines[SSD1306_X1_TEXT_LINES][SSD1306_X1_TEXT_LINE_LEN]);