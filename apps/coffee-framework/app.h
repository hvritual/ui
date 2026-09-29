#ifndef COFFEE_FRAMEWORK_APP_H
#define COFFEE_FRAMEWORK_APP_H
#include "hosts/linux/ui/scene.h"
#include "hosts/linux/ui/navigation.h"
#include "hosts/linux/ui/overlay.h"
#include "hosts/linux/ui/interaction.h"
#include "hosts/linux/ui/reactive.h"
#include "hosts/linux/ui/model.h"

typedef struct { void *impl; } CoffeeApp;
typedef struct {
    unsigned page, selected, first, item_count, locale, theme, progress;
    unsigned completed, modal, nodes, pool, peak_pool, actions;
    uint64_t recycled;
} CoffeeAppStats;
int coffee_app_init(CoffeeApp *app,unsigned height,unsigned item_count);
void coffee_app_dispose(CoffeeApp *app);
PocketInteractionRuntime *coffee_app_interaction(CoffeeApp *app);
int coffee_app_step(CoffeeApp *app,uint64_t monotonic_ms);
int coffee_app_scene(CoffeeApp *app,PocketScene *out);
int coffee_app_stats(const CoffeeApp *app,CoffeeAppStats *out);
#endif
