#pragma once
#include <stdint.h>
struct station_config { uint8_t ssid[32]; uint8_t password[64]; };
bool wifi_station_get_config_default(struct station_config *);
bool wifi_station_get_config(struct station_config *);
bool wifi_station_set_config_current(struct station_config *);
bool wifi_station_set_config(struct station_config *);
