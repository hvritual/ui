#ifndef POCKET_APPLICATION_H
#define POCKET_APPLICATION_H
#include "program.h"
#include "policy.h"
#include "../ui/scene.h"
#include "../ui/navigation.h"
#include "../ui/overlay.h"
#include "../ui/interaction.h"
#include "../ui/reactive.h"
#include "../ui/model.h"
#include "../text-input/keyboard.h"

/* Engine-neutral, experimental application host. Payloads are trusted local
 * development inputs here; package authentication/admission is separate work. */
typedef struct { void *impl; } PocketApplication;
typedef struct {
    unsigned page, selected, first, item_count, locale, theme, progress;
    unsigned completed, modal, nodes, pool, peak_pool, actions;
    uint64_t recycled;
    int scroll_x;
    unsigned scroll_dragging, scroll_settling;
    uint64_t layout_runs;
    unsigned editor_opens, editor_confirms, editor_cancels, editor_active;
    uint64_t ime_commits,ime_candidate_batches;
    int ime_provider_status,ime_provider_stage,ime_provider_errno;
    unsigned ime_provider_storage,ime_input_locale;
} PocketApplicationStats;
int pocket_application_open(PocketApplication *application, unsigned height,
                            unsigned item_count, const char *source, size_t length);
int pocket_application_open_policy(PocketApplication *application, unsigned height,
                                   unsigned item_count, const char *source, size_t length,
                                   const PocketApplicationPolicy *policy);
int pocket_application_open_input(PocketApplication *,unsigned,unsigned,const char *,size_t,
                                   const PocketApplicationPolicy *,const void *,size_t,const void *,size_t);
int pocket_application_policy_snapshot(const PocketApplication *application, PocketApplicationPolicy *out);
void pocket_application_close(PocketApplication *application);
PocketInteractionRuntime *pocket_application_interaction(PocketApplication *application);
/* Drain bounded lifecycle events before dispatching a new external input. */
int pocket_application_prepare_input(PocketApplication *application);
int pocket_application_step(PocketApplication *application, uint64_t monotonic_ms);
int pocket_application_scene(PocketApplication *application, PocketScene *scene);
int pocket_application_stats(const PocketApplication *application, PocketApplicationStats *stats);
int pocket_application_keyboard_snapshot(const PocketApplication *application, PocketKeyboardSnapshot *snapshot);
int pocket_application_keyboard_input_snapshot(const PocketApplication *,PocketKeyboardInputSnapshot *);
int pocket_application_snapshot_allowed(const PocketApplication *application);
const char *pocket_application_error(const PocketApplication *application);
#endif
