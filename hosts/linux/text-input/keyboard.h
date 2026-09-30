#ifndef POCKET_TEXT_KEYBOARD_H
#define POCKET_TEXT_KEYBOARD_H
#include "session.h"
#include "../ui/interaction.h"

/* Local ASCII keyboard, independent of UI locale and machine business logic.
 * Character refs 1000+ASCII and labels 200..229 must exist in the host catalog. */
#define POCKET_KEYBOARD_MAX_FIELDS 4U
#define POCKET_KEYBOARD_MAX_CHARS 64U
#define POCKET_KEYBOARD_TEXT_BASE 200U
#define POCKET_KEYBOARD_ASCII_BASE 1000U

typedef enum {
    POCKET_KEYBOARD_ASCII, POCKET_KEYBOARD_NUMBER,
    POCKET_KEYBOARD_PASSWORD, POCKET_KEYBOARD_PIN
} PocketKeyboardMode;
typedef enum {
    POCKET_KEYBOARD_EDITING, POCKET_KEYBOARD_CONFIRMED, POCKET_KEYBOARD_CANCELLED
} PocketKeyboardResult;
typedef struct {
    uint64_t field_id, label_ref;
    PocketKeyboardMode mode;
    uint32_t max_chars;
    uint8_t enabled, read_only;
    const char *initial;
} PocketKeyboardField;
typedef struct {
    PocketUiTree *tree;
    PocketLayoutContext *layout;
    PocketComponentRuntime *components;
    PocketInteractionRuntime *interaction;
    PocketOverlayManager *overlays;
    uint32_t height, field_count;
    uint64_t overlay_id, owner_route;
    uint64_t page_style, text_style, muted_style, field_style;
    uint64_t key_style, primary_style;
    PocketKeyboardField fields[POCKET_KEYBOARD_MAX_FIELDS];
} PocketKeyboardConfig;
typedef struct { void *impl; } PocketKeyboard;
/* Metadata only. Never serializes key values, text, text hashes or selection. */
typedef struct {
    PocketKeyboardResult result;
    uint32_t active_field, field_count;
    uint8_t visible, sensitive;
    PocketTextStatus last_edit_status;
} PocketKeyboardSnapshot;

int pocket_keyboard_open(PocketKeyboard *keyboard,const PocketKeyboardConfig *config);
/* Call after F6 dispatch/tick, on the same UI thread. Never mutates the tree
 * from an event callback. The caller continues driving existing F6. */
int pocket_keyboard_step(PocketKeyboard *keyboard,uint64_t monotonic_ms);
int pocket_keyboard_snapshot(const PocketKeyboard *keyboard,PocketKeyboardSnapshot *out);
PocketUiHandle pocket_keyboard_root(const PocketKeyboard *keyboard);
/* Explicit trusted-owner getter. Only confirmed values can be copied. It is
 * the owner's responsibility to erase its copies; cancelled drafts never leave. */
PocketTextStatus pocket_keyboard_copy_result(const PocketKeyboard *keyboard,uint32_t field,
                                             char *out,size_t capacity,size_t *bytes);
void pocket_keyboard_dispose(PocketKeyboard *keyboard);
#endif
