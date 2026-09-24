#ifndef COFFEE_VIDEO_PLAN_H
#define COFFEE_VIDEO_PLAN_H
#include "wire.h"
typedef struct { unsigned kind,width,height,fps_num,fps_den,bytes; int fd; } VideoAsset;
typedef struct { unsigned asset,hold_ms; } VideoItem;
typedef struct {
 VideoAsset assets[VIDEO_ASSET_LIMIT];
 VideoItem items[VIDEO_ITEM_LIMIT];
 unsigned asset_count,item_count,loop,idle_ms,poster;
 char generation[65];
} VideoPlan;
void video_plan_init(VideoPlan *plan);
void video_plan_close(VideoPlan *plan);
/* 1 new pinned immutable generation; 0 same/missing; -1 invalid. Old plan is untouched. */
int video_plan_load(const char *root,const char *current_generation,VideoPlan *candidate);
#endif
