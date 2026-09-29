#ifndef POCKET_UI_LAYOUT_H
#define POCKET_UI_LAYOUT_H

#include "object.h"
#include <stdint.h>

#define POCKET_LAYOUT_MAX_CHILDREN 256U

typedef enum {
    POCKET_LAYOUT_LEAF = 0,
    POCKET_LAYOUT_ROW = 1,
    POCKET_LAYOUT_COLUMN = 2,
    POCKET_LAYOUT_STACK = 3,
    POCKET_LAYOUT_GRID = 4,
    POCKET_LAYOUT_ABSOLUTE = 5
} PocketLayoutMode;

typedef enum {
    POCKET_LENGTH_AUTO = 0,
    POCKET_LENGTH_PX = 1,
    POCKET_LENGTH_PERCENT = 2,
    POCKET_LENGTH_CONTENT = 3,
    POCKET_LENGTH_FILL = 4
} PocketLengthKind;

typedef struct {
    PocketLengthKind kind;
    int32_t value; /* px for PX; basis points (0..10000) for PERCENT */
} PocketLength;

typedef struct {
    int32_t left, top, right, bottom;
} PocketLayoutInsets;

typedef enum {
    POCKET_ALIGN_AUTO = 0,
    POCKET_ALIGN_START = 1,
    POCKET_ALIGN_CENTER = 2,
    POCKET_ALIGN_END = 3,
    POCKET_ALIGN_STRETCH = 4
} PocketLayoutAlign;

typedef enum {
    POCKET_JUSTIFY_START = 0,
    POCKET_JUSTIFY_CENTER = 1,
    POCKET_JUSTIFY_END = 2,
    POCKET_JUSTIFY_SPACE_BETWEEN = 3
} PocketLayoutJustify;

typedef enum {
    POCKET_OVERFLOW_VISIBLE = 0,
    POCKET_OVERFLOW_CLIP = 1
} PocketLayoutOverflow;

typedef struct {
    PocketLayoutMode mode;
    PocketLength width, height;
    PocketLength offset_x, offset_y;
    int32_t min_width, min_height;
    int32_t max_width, max_height; /* -1 means unbounded */
    PocketLayoutInsets margin, padding;
    int32_t gap;
    uint16_t grow, shrink;
    PocketLayoutAlign align_items, align_self;
    PocketLayoutJustify justify_content;
    uint16_t grid_columns;
    uint16_t aspect_num, aspect_den;
    uint8_t wrap;
    PocketLayoutOverflow overflow;
} PocketLayoutSpec;

typedef struct {
    PocketUiRect geometry;
    PocketUiRect clip;
    uint8_t clip_valid;
} PocketLayoutResult;

typedef int (*PocketLayoutMeasureFn)(void *context, PocketUiHandle node,
                                     int32_t max_width, int32_t max_height,
                                     int32_t *width, int32_t *height);

typedef struct {
    PocketUiTree *tree;
    PocketLayoutInsets safe_area;
    uint32_t record_capacity;
    PocketLayoutMeasureFn measure;
    void *measure_context;
} PocketLayoutConfig;

typedef struct { void *impl; } PocketLayoutContext;

PocketLayoutSpec pocket_layout_spec_default(void);
PocketUiStatus pocket_layout_init(PocketLayoutContext *context, const PocketLayoutConfig *config);
void pocket_layout_dispose(PocketLayoutContext *context);
PocketUiStatus pocket_layout_set(PocketLayoutContext *context, PocketUiHandle node,
                                 const PocketLayoutSpec *spec);
PocketUiStatus pocket_layout_get(const PocketLayoutContext *context, PocketUiHandle node,
                                 PocketLayoutSpec *out);
PocketUiStatus pocket_layout_result(const PocketLayoutContext *context, PocketUiHandle node,
                                    PocketLayoutResult *out);
PocketUiStatus pocket_layout_run(PocketLayoutContext *context, PocketUiHandle root,
                                 int32_t viewport_width, int32_t viewport_height);

#endif
