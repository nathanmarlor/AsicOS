#pragma once

#include "display.h"

#define DISPLAY_TASK_STACK_SIZE 8192
#define DISPLAY_TASK_PRIORITY   3

// Start the display task with a screen render callback.
// The callback is invoked every 2 seconds to draw the current screen.
void display_task_start(display_screen_cb_t render_cb);
