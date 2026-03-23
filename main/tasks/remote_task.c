#include "remote_task.h"
#include "remote_access.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

void remote_task_start(void)
{
    xTaskCreate(remote_task_loop, "remote", REMOTE_TASK_STACK_SIZE, NULL,
                REMOTE_TASK_PRIORITY, NULL);
}
