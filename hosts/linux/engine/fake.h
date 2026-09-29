#ifndef POCKET_ENGINE_FAKE_H
#define POCKET_ENGINE_FAKE_H

#include "contract.h"

#define POCKET_FAKE_ENGINE_MAX_NODES 64U
#define POCKET_FAKE_ENGINE_MAX_RESOURCES 16U

typedef struct {
    uint32_t generation;
    uint32_t kind;
    PocketEngineNode parent;
    PocketEngineNodeUpdate update;
    int live;
} PocketFakeNode;

typedef struct {
    uint32_t generation;
    uint32_t kind;
    size_t length;
    int live;
} PocketFakeResource;

typedef struct {
    int opened;
    uint32_t width;
    uint32_t height;
    PocketEngineCapabilities capabilities;
    uint8_t *pixels;
    size_t pixels_length;
    PocketEngineRect damage;
    int damage_valid;
    uint32_t next_deadline_ms;
    PocketFakeNode nodes[POCKET_FAKE_ENGINE_MAX_NODES];
    PocketFakeResource resources[POCKET_FAKE_ENGINE_MAX_RESOURCES];
} PocketFakeEngine;

extern const PocketEngineApi pocket_fake_engine_api;

#endif
