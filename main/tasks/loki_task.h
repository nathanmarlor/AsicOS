#pragma once
#define LOKI_TASK_STACK_SIZE 4096
#define LOKI_TASK_PRIORITY   1

void loki_task_start(void);
void loki_push_log(const char *level, const char *msg);
