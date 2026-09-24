#ifndef COFFEE_VIDEO_WIRE_H
#define COFFEE_VIDEO_WIRE_H
#include <stddef.h>
#include <stdint.h>
#define VIDEO_WIDTH_MAX 640U
#define VIDEO_HEIGHT_MAX 360U
#define VIDEO_FRAME_BYTES (VIDEO_WIDTH_MAX*VIDEO_HEIGHT_MAX*4U)
#define VIDEO_WIRE_BYTES 64U
#define VIDEO_FILE_BYTES (8U*1024U*1024U)
#define VIDEO_DURATION_US 60000000ULL
#define VIDEO_FRAME_LIMIT 1800U
#define VIDEO_ASSET_LIMIT 8U
#define VIDEO_ITEM_LIMIT 16U
#define VIDEO_INDEX_HEADER 64U
#define VIDEO_INDEX_ASSET 112U
#define VIDEO_INDEX_ITEM 8U
#define VIDEO_INDEX_MAX (VIDEO_INDEX_HEADER+VIDEO_ASSET_LIMIT*VIDEO_INDEX_ASSET+VIDEO_ITEM_LIMIT*VIDEO_INDEX_ITEM)
static inline uint32_t video_u32(const uint8_t *p){return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);}
static inline uint64_t video_u64(const uint8_t *p){return video_u32(p)|((uint64_t)video_u32(p+4)<<32);}
static inline void video_put32(uint8_t *p,uint32_t v){for(unsigned i=0;i<4;i++)p[i]=(uint8_t)(v>>(i*8));}
static inline void video_put64(uint8_t *p,uint64_t v){video_put32(p,(uint32_t)v);video_put32(p+4,(uint32_t)(v>>32));}
static inline int video_dimensions(unsigned w,unsigned h){return w>=16&&w<=VIDEO_WIDTH_MAX&&h>=16&&h<=VIDEO_HEIGHT_MAX;}
#endif
