#ifndef POCKET_ENGINE_DRAWING_H
#define POCKET_ENGINE_DRAWING_H
#include "contract.h"
/* Independent bounded drawing resource. PSC1 and the upstream Core are unchanged.
 * Polyline points are local integer pixels, copied synchronously, never pointers
 * supplied by application code. Raster textures are private adapter resources. */
#define POCKET_DRAW_VERSION 1U
#define POCKET_DRAW_RESOURCE 0x44525731U
#define POCKET_DRAW_SURFACES 4U
#define POCKET_DRAW_POINTS 4096U
#define POCKET_DRAW_PIXELS 262144U
#define POCKET_DRAW_WIRE_MAGIC 0x31574450U
#define POCKET_DRAW_WIRE_HEADER 32U
typedef struct { uint16_t x,y; } PocketDrawPoint;
typedef struct {
    uint64_t id;
    uint32_t revision,width,height,color,count;
    PocketDrawPoint points[POCKET_DRAW_POINTS];
} PocketDrawSurface;
typedef struct {
    uint32_t version,count;
    PocketDrawSurface surfaces[POCKET_DRAW_SURFACES];
} PocketDrawFrame;
#endif
