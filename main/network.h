#pragma once

#include "config.h"

void send_http_request(sensor_data_t* data);

bool is_wifi_connected();

void init_network();