#pragma once
#include "esp_err.h"
#include <stdbool.h>

esp_err_t plug_sense_init(int gpio);
bool plug_sense_is_connected(void);
