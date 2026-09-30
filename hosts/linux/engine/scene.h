#ifndef POCKET_ENGINE_SCENE_H
#define POCKET_ENGINE_SCENE_H
#include "contract.h"
/* Internal engine-neutral scene upload contract. Not an external package format.
 * resource_create copies synchronously; no caller-owned memory is retained. */
#define POCKET_SCENE_VERSION 1U
#define POCKET_SCENE_MAX_RECORDS 256U
#define POCKET_SCENE_RESOURCE 0x53434e31U
/* Private little-endian PSC1 wire: 8 u32 header words, then 20 u32 words
 * per record. Stable IDs occupy two words; all other fields are bounded u32/i32.
 * No native struct padding, pointer, float, endianness or JS JSON dependency. */
#define POCKET_SCENE_WIRE_MAGIC 0x31435350U
#define POCKET_SCENE_WIRE_HEADER 32U
#define POCKET_SCENE_WIRE_RECORD 80U
#define POCKET_SCENE_WIRE_MAX (POCKET_SCENE_WIRE_HEADER+POCKET_SCENE_MAX_RECORDS*POCKET_SCENE_WIRE_RECORD)

typedef struct {
    uint64_t id;
    uint32_t kind;
    PocketEngineRect bounds, clip;
    uint32_t background, foreground;
    uint16_t radius, opacity_256;
    uint64_t text_ref, resource_ref;
    int32_t value, minimum, maximum;
} PocketSceneRecord;

typedef struct {
    uint32_t version, width, height, count;
    uint32_t locale, background;
    int media_ready;
    PocketSceneRecord records[POCKET_SCENE_MAX_RECORDS];
} PocketScene;
#endif
