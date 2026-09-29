#ifndef POCKET_ENGINE_CURRENT_H
#define POCKET_ENGINE_CURRENT_H

#include "contract.h"

typedef struct {
    void *context;
    PocketEngineStatus (*open)(void *context, const PocketEngineOpenConfig *config);
    PocketEngineStatus (*close)(void *context);
    PocketEngineStatus (*tick)(void *context, uint64_t monotonic_ms, uint32_t *next_deadline_ms);
    PocketEngineStatus (*render)(void *context, PocketEngineFrame *out);
} PocketCurrentRendererHooks;

typedef struct {
    int opened;
    PocketCurrentRendererHooks hooks;
} PocketCurrentEngine;

void pocket_current_engine_init(PocketCurrentEngine *engine,
                                const PocketCurrentRendererHooks *hooks);

extern const PocketEngineApi pocket_current_engine_api;

#endif
