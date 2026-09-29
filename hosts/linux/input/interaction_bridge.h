#ifndef POCKET_INPUT_INTERACTION_BRIDGE_H
#define POCKET_INPUT_INTERACTION_BRIDGE_H

#include "state.h"
#include "../ui/interaction.h"
#include <stdint.h>

typedef struct {
    int active;
    int delivered;
    int id;
    int x;
    int y;
} PocketInputInteractionContact;

typedef struct {
    PocketInteractionRuntime *interaction;
    PocketInputInteractionContact contacts[INPUT_RUNTIME_MAX_CONTACTS];
    uint32_t count;
    uint64_t frames;
    uint64_t delivered;
    uint64_t cancels;
    uint64_t ignored_no_target;
} PocketInputInteractionBridge;

int pocket_input_interaction_bridge_init(PocketInputInteractionBridge *bridge,
                                         PocketInteractionRuntime *interaction);
int pocket_input_interaction_bridge_frame(PocketInputInteractionBridge *bridge,
                                          const InputFrame *frame,uint64_t event_ns);
void pocket_input_interaction_bridge_disconnect(PocketInputInteractionBridge *bridge,
                                                uint64_t event_ns);

#endif
