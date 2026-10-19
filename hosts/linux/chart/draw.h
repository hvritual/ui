#ifndef POCKET_CHART_DRAW_H
#define POCKET_CHART_DRAW_H
#include <stddef.h>
#include <stdint.h>

#define POCKET_CHART_POINTS 1000u
#define POCKET_CHART_WIDTH 768u
#define POCKET_CHART_HEIGHT 320u
#define POCKET_CHART_WIRE_MAGIC 0x31484350u /* "PCH1" little endian */
#define POCKET_CHART_HEADER_BYTES 32u
#define POCKET_CHART_MAX_PACKET (POCKET_CHART_HEADER_BYTES+POCKET_CHART_WIDTH*POCKET_CHART_HEIGHT*4u)

typedef enum {
    POCKET_CHART_LINE=1, POCKET_CHART_BAR=2,
    POCKET_CHART_SCATTER=3, POCKET_CHART_BUBBLE=4
} PocketChartMode;

/* Coordinate units 0..10000, size is 0..255. One plotted dataset takes
 * <= 5000 bytes rather than >1000 Scene nodes; no app pointers cross worlds. */
typedef struct {
    uint16_t x,y;
    uint8_t size;
} PocketChartPoint;
typedef struct {
    PocketChartPoint points[POCKET_CHART_POINTS];
    uint16_t width,height,count;
    uint8_t mode;
    uint32_t color, revision;
} PocketChartModel;

/* Strict RFC4648 base64 containing count*5 octets, canonical padding only. */
int pocket_chart_decode(PocketChartModel *out,unsigned mode,uint32_t color,
                        const char *base64,size_t encoded_bytes);
/* Allocates an exact PCH1 resource bitmap. Caller owns *packet; rendering is
 * bounded by fixed dimensions and has no external font/texture dependency. */
int pocket_chart_packet(const PocketChartModel *model,uint64_t stable_id,
                        uint8_t **packet,size_t *bytes);
#endif
