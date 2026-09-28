#ifndef POCKET_LVGL_WORKLOAD_H
#define POCKET_LVGL_WORKLOAD_H
#include "adapter.h"
#include <stdint.h>

typedef struct {
    PocketNode progress;
    PocketNode progress_label;
    uint32_t last_step;
} PocketCoffeeWorkload;

int pocket_coffee_build(PocketLvglEngine *engine, PocketCoffeeWorkload *workload);
int pocket_coffee_progress_step(PocketLvglEngine *engine, PocketCoffeeWorkload *workload, uint32_t elapsed_ms);

#endif
