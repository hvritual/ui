#include "workload.h"
#include <stdio.h>
#include <string.h>

int pocket_coffee_build(PocketLvglEngine *engine, PocketCoffeeWorkload *workload) {
    static const char *names[8] = {
        "Espresso", "Americano", "Latte", "Cappuccino",
        "Flat White", "Mocha", "Tea", "Hot Water"
    };
    if(!engine || !workload) return 0;
    memset(workload, 0, sizeof(*workload));
    const uint32_t h = pocket_engine_height(engine);
    if(!pocket_engine_label(engine, 32, 20, 620, "Coffee Machine", 0x303734)) return 0;
    if(!pocket_engine_label(engine, 32, 55, 640, "Pocket F0 frozen structural workload", 0x71766f)) return 0;
    if(!pocket_engine_button(engine, 832, 24, 160, 48, "en-US", 0xe7ece1)) return 0;

    const int32_t top = 110;
    const int32_t bottom = (int32_t)h - 150;
    const int32_t available = bottom - top;
    const int32_t row_h = available / 2 - 8;
    for(int i = 0; i < 8; ++i) {
        const int32_t col = i % 4;
        const int32_t row = i / 4;
        const int32_t x = 24 + col * 248;
        const int32_t y = top + row * (row_h + 16);
        if(!pocket_engine_box(engine, x, y, 232, row_h, 0xffffff, 12)) return 0;
        if(!pocket_engine_label(engine, x + 16, y + row_h - 42, 200, names[i], 0x303734)) return 0;
    }
    workload->progress = pocket_engine_bar(engine, 256, (int32_t)h - 94, 512, 14, 0x34634b);
    workload->progress_label = pocket_engine_label(engine, 472, (int32_t)h - 72, 120, "0%", 0x303734);
    return workload->progress && workload->progress_label;
}

int pocket_coffee_progress_step(PocketLvglEngine *engine, PocketCoffeeWorkload *workload, uint32_t elapsed_ms) {
    if(!engine || !workload) return 0;
    const uint32_t step = elapsed_ms / 100U;
    if(step == workload->last_step) return 1;
    workload->last_step = step;
    const int32_t value = (int32_t)((step * 7U) % 101U);
    char text[16];
    if(snprintf(text, sizeof(text), "%d%%", value) <= 0) return 0;
    return pocket_engine_set_bar(engine, workload->progress, value) &&
           pocket_engine_set_label(engine, workload->progress_label, text);
}
