#include "interaction_bridge.h"

#include <string.h>

static int find_contact(const PocketInputInteractionBridge *bridge,int id){
    for(uint32_t i=0;i<bridge->count;i++)
        if(bridge->contacts[i].active&&bridge->contacts[i].id==id)return (int)i;
    return -1;
}
static int frame_has(const InputFrame *frame,int id){
    for(uint32_t i=0;i<frame->contact_count;i++)if(frame->contacts[i].id==id)return 1;
    return 0;
}
static int frame_cancelled(const InputFrame *frame,int id){
    for(uint32_t i=0;i<frame->cancelled_count;i++)if(frame->cancelled[i]==id)return 1;
    return 0;
}
/* Reject the entire snapshot before delivering any edge. Corrupt contact IDs
 * must not leave a partially-applied gesture active. */
static int valid_frame(const InputFrame *frame){
    if(!frame||frame->contact_count>INPUT_RUNTIME_MAX_CONTACTS||
       frame->cancelled_count>INPUT_RUNTIME_MAX_CONTACTS)return 0;
    for(uint32_t i=0;i<frame->contact_count;i++){
        if(frame->contacts[i].id<0)return 0;
        for(uint32_t j=0;j<i;j++)if(frame->contacts[j].id==frame->contacts[i].id)return 0;
        for(uint32_t j=0;j<frame->cancelled_count;j++)if(frame->cancelled[j]==frame->contacts[i].id)return 0;
    }
    for(uint32_t i=0;i<frame->cancelled_count;i++){
        if(frame->cancelled[i]<0)return 0;
        for(uint32_t j=0;j<i;j++)if(frame->cancelled[j]==frame->cancelled[i])return 0;
    }
    return 1;
}
static int terminal_status(PocketInteractionStatus status){
    return status==POCKET_INTERACTION_OK||status==POCKET_INTERACTION_STALE_HANDLE||
           status==POCKET_INTERACTION_POINTER_NOT_ACTIVE;
}
static uint64_t to_ms(uint64_t event_ns){
    return event_ns/1000000ULL;
}
int pocket_input_interaction_bridge_init(PocketInputInteractionBridge *bridge,
                                         PocketInteractionRuntime *interaction){
    if(!bridge||!interaction)return 0;
    memset(bridge,0,sizeof(*bridge));
    bridge->interaction=interaction;
    return 1;
}
int pocket_input_interaction_bridge_frame(PocketInputInteractionBridge *bridge,
                                          const InputFrame *frame,uint64_t event_ns){
    if(!bridge||!bridge->interaction)return 0;
    if(!valid_frame(frame)){
        pocket_input_interaction_bridge_disconnect(bridge,event_ns);
        return 0;
    }
    bridge->frames++;
    if(frame->syn_dropped||frame->suppressed){
        pocket_interaction_cancel_all(bridge->interaction,to_ms(event_ns));
        bridge->cancels+=bridge->count;
        memset(bridge->contacts,0,sizeof(bridge->contacts));
        bridge->count=0;
        return 1;
    }

    for(uint32_t i=0;i<frame->cancelled_count;i++){
        int index=find_contact(bridge,frame->cancelled[i]);
        if(index<0)continue;
        PocketInputInteractionContact *contact=&bridge->contacts[index];
        if(contact->delivered){
            PocketPointerEvent event={(uint32_t)contact->id,POCKET_POINTER_CANCEL,
                                      contact->x,contact->y,to_ms(event_ns)};
            PocketInteractionStatus status=pocket_interaction_pointer(bridge->interaction,&event);
            if(!terminal_status(status))return 0;
            bridge->cancels++;
        }
        contact->active=0;
    }

    for(uint32_t i=0;i<bridge->count;i++){
        PocketInputInteractionContact *previous=&bridge->contacts[i];
        if(!previous->active||frame_has(frame,previous->id)||frame_cancelled(frame,previous->id))continue;
        if(previous->delivered){
            PocketPointerEvent event={(uint32_t)previous->id,POCKET_POINTER_UP,
                                      previous->x,previous->y,to_ms(event_ns)};
            PocketInteractionStatus status=pocket_interaction_pointer(bridge->interaction,&event);
            if(!terminal_status(status))return 0;
            bridge->delivered++;
        }
        previous->active=0;
    }

    PocketInputInteractionContact next[INPUT_RUNTIME_MAX_CONTACTS];
    memset(next,0,sizeof(next));
    for(uint32_t i=0;i<frame->contact_count;i++){
        const InputContact *contact=&frame->contacts[i];
        if(contact->id<0)return 0;
        int previous=find_contact(bridge,contact->id);
        int delivered=0;
        if(previous>=0&&bridge->contacts[previous].delivered){
            PocketPointerEvent event={(uint32_t)contact->id,POCKET_POINTER_MOVE,
                                      contact->x,contact->y,to_ms(event_ns)};
            PocketInteractionStatus status=pocket_interaction_pointer(bridge->interaction,&event);
            if(status==POCKET_INTERACTION_OK){delivered=1;bridge->delivered++;}
            else if(!terminal_status(status))return 0;
        }else if(previous<0){
            PocketPointerEvent event={(uint32_t)contact->id,POCKET_POINTER_DOWN,
                                      contact->x,contact->y,to_ms(event_ns)};
            PocketInteractionStatus status=pocket_interaction_pointer(bridge->interaction,&event);
            if(status==POCKET_INTERACTION_OK){delivered=1;bridge->delivered++;}
            else if(status==POCKET_INTERACTION_NO_TARGET)bridge->ignored_no_target++;
            else return 0;
        }
        next[i]=(PocketInputInteractionContact){1,delivered,contact->id,contact->x,contact->y};
    }
    memcpy(bridge->contacts,next,sizeof(next));
    bridge->count=frame->contact_count;
    return 1;
}
void pocket_input_interaction_bridge_disconnect(PocketInputInteractionBridge *bridge,
                                                uint64_t event_ns){
    if(!bridge||!bridge->interaction)return;
    pocket_interaction_cancel_all(bridge->interaction,to_ms(event_ns));
    for(uint32_t i=0;i<bridge->count;i++)if(bridge->contacts[i].active&&bridge->contacts[i].delivered)
        bridge->cancels++;
    memset(bridge->contacts,0,sizeof(bridge->contacts));
    bridge->count=0;
}
