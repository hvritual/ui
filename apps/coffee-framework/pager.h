#ifndef COFFEE_PAGER_H
#define COFFEE_PAGER_H
#include <stdint.h>

#define COFFEE_PAGE_WIDTH 984
#define COFFEE_PAGE_ITEMS 6U
#define COFFEE_PAGE_POOL 12U
#define COFFEE_SNAP_MS 180U

/* Logical-pixel paging state only. No renderer/device APIs or allocations.
 * A press can interrupt settling at its current position without selecting a card.
 */
typedef struct {
    unsigned pages, settled_page;
    int position, down_position, down_x;
    int active, dragging, settling, block_tap;
    uint32_t pointer;
    int from, to;
    uint64_t since, last_move_ms;
    int last_x, velocity;
} CoffeePager;
void coffee_pager_init(CoffeePager *pager,unsigned item_count);
void coffee_pager_tick(CoffeePager *pager,uint64_t ms);
void coffee_pager_press(CoffeePager *pager,uint32_t pointer,int x,uint64_t ms);
void coffee_pager_move(CoffeePager *pager,uint32_t pointer,int x,uint64_t ms);
void coffee_pager_release(CoffeePager *pager,uint32_t pointer,int x,uint64_t ms);
void coffee_pager_cancel(CoffeePager *pager);
void coffee_pager_jump(CoffeePager *pager,unsigned page);
#endif
