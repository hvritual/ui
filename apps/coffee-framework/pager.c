#include "pager.h"
#include <string.h>

static int limit(const CoffeePager *p) { return (int)(p->pages-1U)*COFFEE_PAGE_WIDTH; }
static int clamp(int v,int lo,int hi) { return v<lo?lo:v>hi?hi:v; }
static int resist(int64_t v,int maximum) {
    if(v<0)return (int)(v< -240?-80:v/3);
    if(v>maximum)return maximum+(int)(v-maximum>240?80:(v-maximum)/3);
    return (int)v;
}
static void settle(CoffeePager *p,int target,uint64_t ms) {
    p->from=p->position;p->to=clamp(target,0,limit(p));p->since=ms;
    p->settling=p->from!=p->to;p->active=p->dragging=0;
    if(!p->settling)p->settled_page=(unsigned)p->to/COFFEE_PAGE_WIDTH;
}
void coffee_pager_init(CoffeePager *p,unsigned count) {
    memset(p,0,sizeof(*p));p->pages=(count+COFFEE_PAGE_ITEMS-1U)/COFFEE_PAGE_ITEMS;
    if(!p->pages)p->pages=1;
}
void coffee_pager_tick(CoffeePager *p,uint64_t ms) {
    if(!p->settling||ms<p->since)return;
    uint64_t elapsed=ms-p->since;
    if(elapsed>=COFFEE_SNAP_MS) {
        p->position=p->to;p->settling=0;
        p->settled_page=(unsigned)p->to/COFFEE_PAGE_WIDTH;return;
    }
    int64_t left=(int64_t)(COFFEE_SNAP_MS-elapsed);
    int64_t total=(int64_t)COFFEE_SNAP_MS*COFFEE_SNAP_MS*COFFEE_SNAP_MS;
    /* Integer cubic ease-out is identical on Native/ARM; no overshoot. */
    p->position=p->to+(int)((int64_t)(p->from-p->to)*left*left*left/total);
}
void coffee_pager_press(CoffeePager *p,uint32_t id,int x,uint64_t ms) {
    if(p->active)return;
    coffee_pager_tick(p,ms);
    p->block_tap=p->settling;p->settling=0;p->active=1;p->dragging=0;
    p->pointer=id;p->down_x=p->last_x=x;p->down_position=p->position;
    p->last_move_ms=ms;p->velocity=0;
}
void coffee_pager_move(CoffeePager *p,uint32_t id,int x,uint64_t ms) {
    if(!p->active||p->pointer!=id)return;
    if(x!=p->last_x&&ms>p->last_move_ms) {
        uint64_t dt=ms-p->last_move_ms;
        int64_t v=((int64_t)x-p->last_x)*1000/(int64_t)(dt>10000?10000:dt);
        p->velocity=(int)(v>5000?5000:v< -5000?-5000:v);
        p->last_x=x;p->last_move_ms=ms;
    }
    p->dragging=1;p->block_tap=1;
    p->position=resist((int64_t)p->down_position+p->down_x-x,limit(p));
}
void coffee_pager_release(CoffeePager *p,uint32_t id,int x,uint64_t ms) {
    if(!p->active||p->pointer!=id)return;
    if(!p->dragging&&!p->block_tap){p->active=0;return;}
    if(p->dragging)coffee_pager_move(p,id,x,ms);
    int pos=clamp(p->position,0,limit(p));
    int target=(pos+COFFEE_PAGE_WIDTH/2)/COFFEE_PAGE_WIDTH;
    int64_t displacement=(int64_t)p->down_x-x;
    int recent=ms>=p->last_move_ms&&ms-p->last_move_ms<=100U;
    int origin=clamp((p->down_position+COFFEE_PAGE_WIDTH/2)/COFFEE_PAGE_WIDTH,0,(int)p->pages-1);
    if(displacement>=COFFEE_PAGE_WIDTH/4 || (recent&&p->velocity<= -600&&displacement>=48))target=origin+1;
    else if(displacement<= -COFFEE_PAGE_WIDTH/4 || (recent&&p->velocity>=600&&displacement<= -48))target=origin-1;
    target=clamp(target,0,(int)p->pages-1);
    settle(p,target*COFFEE_PAGE_WIDTH,ms);
}
void coffee_pager_cancel(CoffeePager *p) {
    p->position=(int)p->settled_page*COFFEE_PAGE_WIDTH;
    p->active=p->dragging=p->settling=0;p->block_tap=1;
}
void coffee_pager_jump(CoffeePager *p,unsigned page) {
    if(page>=p->pages)page=p->pages-1;
    p->settled_page=page;p->position=(int)page*COFFEE_PAGE_WIDTH;
    p->active=p->dragging=p->settling=p->block_tap=0;
}
