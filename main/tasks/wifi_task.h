#pragma once

#include <stdbool.h>

#define WIFI_TASK_STACK_SIZE 4096
#define WIFI_TASK_PRIORITY   3

void wifi_task_start(void);
bool wifi_is_connected(void);
