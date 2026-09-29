#ifndef POCKET_UI_STYLE_H
#define POCKET_UI_STYLE_H

#include <stddef.h>
#include <stdint.h>

typedef enum {
    POCKET_STYLE_OK = 0,
    POCKET_STYLE_INVALID_ARGUMENT = 1,
    POCKET_STYLE_NOT_FOUND = 2,
    POCKET_STYLE_RESOURCE_EXHAUSTED = 3,
    POCKET_STYLE_TOKEN_MISSING = 4
} PocketStyleStatus;

typedef enum {
    POCKET_STYLE_BACKGROUND = 0,
    POCKET_STYLE_FOREGROUND = 1,
    POCKET_STYLE_BORDER_COLOR = 2,
    POCKET_STYLE_BORDER_WIDTH = 3,
    POCKET_STYLE_RADIUS = 4,
    POCKET_STYLE_FONT_ID = 5,
    POCKET_STYLE_FONT_SIZE = 6,
    POCKET_STYLE_SPACING = 7,
    POCKET_STYLE_OPACITY = 8,
    POCKET_STYLE_TRANSLATE_X = 9,
    POCKET_STYLE_TRANSLATE_Y = 10,
    POCKET_STYLE_FIELD_COUNT = 11
} PocketStyleField;

#define POCKET_STYLE_BIT(field) (1U << (field))
#define POCKET_STYLE_ALL ((1U << POCKET_STYLE_FIELD_COUNT) - 1U)
#define POCKET_STYLE_INHERITED (POCKET_STYLE_BIT(POCKET_STYLE_FOREGROUND) |                                 POCKET_STYLE_BIT(POCKET_STYLE_FONT_ID) |                                 POCKET_STYLE_BIT(POCKET_STYLE_FONT_SIZE))

typedef enum {
    POCKET_STYLE_LITERAL = 0,
    POCKET_STYLE_TOKEN = 1
} PocketStyleAtomSource;

typedef struct {
    PocketStyleAtomSource source;
    uint32_t token;
    int64_t value;
} PocketStyleAtom;

typedef struct {
    uint32_t set_mask;
    PocketStyleAtom fields[POCKET_STYLE_FIELD_COUNT];
} PocketStyle;

typedef struct {
    uint32_t set_mask;
    int64_t fields[POCKET_STYLE_FIELD_COUNT];
} PocketResolvedStyle;

enum {
    POCKET_STATE_PRESSED  = 1U << 0,
    POCKET_STATE_FOCUSED  = 1U << 1,
    POCKET_STATE_DISABLED = 1U << 2,
    POCKET_STATE_CHECKED  = 1U << 3,
    POCKET_STATE_SELECTED = 1U << 4,
    POCKET_STATE_ALL = (1U << 5) - 1U
};

typedef struct {
    uint32_t id;
    int64_t value;
} PocketThemeToken;

typedef struct {
    uint32_t id;
    PocketStyle base;
    const PocketThemeToken *tokens;
    uint32_t token_count;
} PocketThemeDefinition;

typedef struct {
    uint64_t style_ref;
    uint32_t required_states;
    uint16_t priority;
    PocketStyle style;
} PocketStyleRule;

typedef struct {
    uint32_t theme_capacity;
    uint32_t token_capacity;
    uint32_t rule_capacity;
} PocketStyleRuntimeConfig;

typedef struct { void *impl; } PocketStyleRuntime;

PocketStyleAtom pocket_style_literal(int64_t value);
PocketStyleAtom pocket_style_token(uint32_t token);
PocketStyleStatus pocket_style_runtime_init(PocketStyleRuntime *runtime,
                                             const PocketStyleRuntimeConfig *config);
void pocket_style_runtime_dispose(PocketStyleRuntime *runtime);
PocketStyleStatus pocket_style_add_theme(PocketStyleRuntime *runtime,
                                         const PocketThemeDefinition *theme);
PocketStyleStatus pocket_style_add_rule(PocketStyleRuntime *runtime,
                                        const PocketStyleRule *rule);
PocketStyleStatus pocket_style_set_theme(PocketStyleRuntime *runtime, uint32_t theme_id);
uint32_t pocket_style_theme(const PocketStyleRuntime *runtime);
uint64_t pocket_style_theme_epoch(const PocketStyleRuntime *runtime);
PocketStyleStatus pocket_style_resolve(const PocketStyleRuntime *runtime,
                                       uint64_t style_ref, uint32_t states,
                                       const PocketResolvedStyle *parent,
                                       PocketResolvedStyle *out);

#endif
