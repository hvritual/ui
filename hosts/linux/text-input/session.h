#ifndef POCKET_TEXT_SESSION_H
#define POCKET_TEXT_SESSION_H

#include <stddef.h>
#include <stdint.h>

#define POCKET_TEXT_API_VERSION 1U
#define POCKET_TEXT_MAX_BYTES 4096U
#define POCKET_TEXT_MAX_PREEDIT_BYTES 1024U

typedef enum {
    POCKET_TEXT_OK = 0,
    POCKET_TEXT_INVALID_ARGUMENT,
    POCKET_TEXT_INVALID_UTF8,
    POCKET_TEXT_INVALID_BOUNDARY,
    POCKET_TEXT_LIMIT,
    POCKET_TEXT_NOT_FOCUSED,
    POCKET_TEXT_STALE_EVENT,
    POCKET_TEXT_READ_ONLY,
    POCKET_TEXT_DISABLED,
    POCKET_TEXT_POLICY,
    POCKET_TEXT_COMPOSING,
    POCKET_TEXT_NOT_COMPOSING,
    POCKET_TEXT_EXHAUSTED
} PocketTextStatus;

typedef enum { POCKET_TEXT_PLAIN, POCKET_TEXT_ASCII, POCKET_TEXT_PIN } PocketTextMode;
typedef enum { POCKET_TEXT_GRAPHEME, POCKET_TEXT_UTF8_BYTE, POCKET_TEXT_UTF16_UNIT } PocketTextUnit;
typedef struct {
    uint32_t max_bytes, max_graphemes;
    PocketTextMode mode;
    uint8_t enabled, read_only, sensitive;
} PocketTextConfig;

typedef struct {
    uint64_t field_id, session_id, focus_generation, engine_generation, revision;
} PocketTextToken;

typedef enum {
    POCKET_TEXT_INSERT = 1, POCKET_TEXT_SELECT, POCKET_TEXT_LEFT, POCKET_TEXT_RIGHT,
    POCKET_TEXT_HOME, POCKET_TEXT_END, POCKET_TEXT_SELECT_ALL, POCKET_TEXT_CLEAR,
    POCKET_TEXT_BACKSPACE, POCKET_TEXT_DELETE, POCKET_TEXT_COMPOSITION_BEGIN,
    POCKET_TEXT_COMPOSITION_UPDATE, POCKET_TEXT_COMPOSITION_COMMIT,
    POCKET_TEXT_COMPOSITION_CANCEL, POCKET_TEXT_ENTER
} PocketTextAction;
typedef enum {
    POCKET_TEXT_EFFECT_NONE, POCKET_TEXT_EFFECT_CHANGED, POCKET_TEXT_EFFECT_COMMITTED,
    POCKET_TEXT_EFFECT_ACCEPT_CANDIDATE, POCKET_TEXT_EFFECT_SUBMIT
} PocketTextEffect;
typedef struct {
    uint32_t version;
    PocketTextToken token;
    PocketTextAction action;
    const char *text;
    size_t text_bytes;
    PocketTextUnit unit;
    uint32_t anchor, focus;
} PocketTextEvent;

typedef struct { void *impl; } PocketTextSession;
/* Metadata only: no committed text, preedit, candidate values or text hashes. */
typedef struct {
    PocketTextToken token;
    uint32_t committed_bytes, graphemes, utf16_units, preedit_bytes;
    uint32_t anchor_grapheme, focus_grapheme;
    uint8_t active, composing, sensitive, enabled, read_only;
} PocketTextSnapshot;

PocketTextConfig pocket_text_config_default(void);
/* Single owning UI thread, matching F6. IDs are unique until process shutdown.
 * No method here changes component focus: its owner relays F6 focus/lifecycle.
 * Initialize a zeroed object. Calling init on a live object is rejected. */
PocketTextStatus pocket_text_init(PocketTextSession *session, uint64_t field_id,
                                  const PocketTextConfig *config,
                                  const char *initial, size_t bytes);
void pocket_text_dispose(PocketTextSession *session);
PocketTextStatus pocket_text_focus(PocketTextSession *session);
PocketTextStatus pocket_text_blur(PocketTextSession *session);
PocketTextStatus pocket_text_engine_reset(PocketTextSession *session);
PocketTextStatus pocket_text_set_flags(PocketTextSession *session, int enabled, int read_only);
PocketTextStatus pocket_text_snapshot(const PocketTextSession *session, PocketTextSnapshot *out);
/* An accepted event advances revision, including no-op edits/Enter. Replaying its
 * old token is rejected. Errors leave ALL session state unchanged. */
PocketTextStatus pocket_text_apply(PocketTextSession *session, const PocketTextEvent *event,
                                   PocketTextEffect *effect);
/* Explicit owner access to business value; never includes preedit. Do not log it.
 * The display getter always masks sensitive fields. No normalization is applied. */
PocketTextStatus pocket_text_copy_committed(const PocketTextSession *session, char *out,
                                            size_t capacity, size_t *bytes);
PocketTextStatus pocket_text_copy_display(const PocketTextSession *session, char *out,
                                          size_t capacity, size_t *bytes);
PocketTextStatus pocket_text_copy_preedit(const PocketTextSession *session, char *out,
                                          size_t capacity, size_t *bytes);
/* Strict conversions: accept only extended-grapheme boundaries. UTF-16 surrogate
 * interiors AND scalar boundaries inside a grapheme are rejected, not rounded. */
PocketTextStatus pocket_text_convert(const char *text, size_t bytes,
                                     PocketTextUnit from, uint32_t offset,
                                     PocketTextUnit to, uint32_t *out);
PocketTextStatus pocket_text_graphemes(const char *text, size_t bytes, uint32_t *out);
const char *pocket_text_unicode_version(void);
#endif
