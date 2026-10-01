#ifndef POCKET_KEY_LAYOUT_H
#define POCKET_KEY_LAYOUT_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define POCKET_KEY_LAYOUT_MAX_KEYS 48U
#define POCKET_KEY_LAYOUT_EN 1U
#define POCKET_KEY_LAYOUT_ZH 2U
typedef enum { POCKET_LAYOUT_LETTERS,POCKET_LAYOUT_SYMBOLS,POCKET_LAYOUT_DIGITS } PocketKeyLayoutMode;
typedef enum { POCKET_KEY_CHAR,POCKET_KEY_SHIFT,POCKET_KEY_BACKSPACE,POCKET_KEY_MODE,POCKET_KEY_LANGUAGE,POCKET_KEY_SPACE,POCKET_KEY_ENTER,POCKET_KEY_HIDE } PocketKeyRole;
typedef struct { int x,y,width,height; } PocketKeyRect;
typedef struct { PocketKeyRole role;uint32_t codepoint;PocketKeyRect bounds; } PocketKeyGeometry;
typedef struct {
    unsigned available_locales,active_locales,locale;
    PocketKeyLayoutMode mode;uint8_t sensitive,shift,popup;
    uint64_t generation;
} PocketKeyLayoutState;
typedef struct {
    PocketKeyRect editor,preedit,candidates,language_popup;
    PocketKeyGeometry keys[POCKET_KEY_LAYOUT_MAX_KEYS];
    unsigned count,language_count,language_choices[2],space_locale;
    uint64_t generation;
} PocketKeyLayout;
/* Geometry/presentation contract only. It neither dispatches touches nor owns
 * focus/overlays; a view must bind these roles to the existing F6/Keyboard. */
int pocket_key_layout_init(PocketKeyLayoutState *,unsigned available,unsigned active,unsigned initial,int sensitive);
int pocket_key_layout_shift(PocketKeyLayoutState *,int shifted);
int pocket_key_layout_mode(PocketKeyLayoutState *,PocketKeyLayoutMode);
int pocket_key_layout_languages(PocketKeyLayoutState *,int show);
/* A successful switch invalidates all held-key and candidate generations.
 * The view must cancel composition via TextSession; it must never commit it. */
int pocket_key_layout_choose(PocketKeyLayoutState *,unsigned locale);
int pocket_key_layout_build(const PocketKeyLayoutState *,unsigned height,PocketKeyLayout *);
#ifdef __cplusplus
}
#endif
#endif
