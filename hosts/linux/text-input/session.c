#include "session.h"
#include <stdlib.h>
#include <string.h>
#include <utf8proc.h>

typedef struct {
    uint16_t byte[POCKET_TEXT_MAX_BYTES + 1U];
    uint16_t utf16[POCKET_TEXT_MAX_BYTES + 1U];
    uint32_t count;
} TextIndex;
typedef struct {
    PocketTextConfig config;
    PocketTextToken token;
    char committed[POCKET_TEXT_MAX_BYTES + 1U];
    char preedit[POCKET_TEXT_MAX_PREEDIT_BYTES + 1U];
    size_t bytes, preedit_bytes;
    TextIndex index;
    uint32_t anchor, focus;
    uint8_t active, composing;
} TextImpl;
/* Owner-thread only; session identity never reuses a freed address or field ID. */
static uint64_t next_session_id = 1U;
static TextImpl *get(const PocketTextSession *s) { return s ? s->impl : NULL; }
static void wipe(void *p, size_t n) {
    volatile unsigned char *v = p;
    while (n--) *v++ = 0;
}
static PocketTextStatus index_text(const char *text, size_t bytes, TextIndex *out) {
    if (!out || (!text && bytes)) return POCKET_TEXT_INVALID_ARGUMENT;
    if (bytes > POCKET_TEXT_MAX_BYTES) return POCKET_TEXT_LIMIT;
    memset(out, 0, sizeof(*out));
    size_t pos = 0; uint32_t units = 0;
    utf8proc_int32_t previous = 0, state = 0;
    while (pos < bytes) {
        utf8proc_int32_t current;
        utf8proc_ssize_t used = utf8proc_iterate((const utf8proc_uint8_t *)text + pos,
                                                (utf8proc_ssize_t)(bytes - pos), &current);
        if (used <= 0) return POCKET_TEXT_INVALID_UTF8;
        if (pos && utf8proc_grapheme_break_stateful(previous, current, &state)) {
            ++out->count;
            out->byte[out->count] = (uint16_t)pos;
            out->utf16[out->count] = (uint16_t)units;
        }
        pos += (size_t)used; units += current > 0xffff ? 2U : 1U; previous = current;
    }
    if (bytes) ++out->count;
    out->byte[out->count] = (uint16_t)bytes;
    out->utf16[out->count] = (uint16_t)units;
    return POCKET_TEXT_OK;
}
static PocketTextStatus boundary(const TextIndex *index, PocketTextUnit unit,
                                 uint32_t offset, uint32_t *out) {
    if (unit < POCKET_TEXT_GRAPHEME || unit > POCKET_TEXT_UTF16_UNIT)
        return POCKET_TEXT_INVALID_ARGUMENT;
    for (uint32_t i = 0; i <= index->count; ++i) {
        uint32_t n = unit == POCKET_TEXT_GRAPHEME ? i :
                     unit == POCKET_TEXT_UTF8_BYTE ? index->byte[i] : index->utf16[i];
        if (n == offset) { *out = i; return POCKET_TEXT_OK; }
    }
    return POCKET_TEXT_INVALID_BOUNDARY;
}
PocketTextStatus pocket_text_convert(const char *text, size_t bytes,
                                     PocketTextUnit from, uint32_t offset,
                                     PocketTextUnit to, uint32_t *out) {
    if (!out || to < POCKET_TEXT_GRAPHEME || to > POCKET_TEXT_UTF16_UNIT)
        return POCKET_TEXT_INVALID_ARGUMENT;
    TextIndex index; uint32_t g;
    PocketTextStatus s = index_text(text, bytes, &index);
    if (s != POCKET_TEXT_OK) return s;
    s = boundary(&index, from, offset, &g);
    if (s != POCKET_TEXT_OK) return s;
    *out = to == POCKET_TEXT_GRAPHEME ? g : to == POCKET_TEXT_UTF8_BYTE ? index.byte[g] : index.utf16[g];
    return POCKET_TEXT_OK;
}
PocketTextStatus pocket_text_graphemes(const char *text, size_t bytes, uint32_t *out) {
    if (!out) return POCKET_TEXT_INVALID_ARGUMENT;
    TextIndex index; PocketTextStatus s = index_text(text, bytes, &index);
    if (s == POCKET_TEXT_OK) *out = index.count;
    return s;
}
const char *pocket_text_unicode_version(void) { return utf8proc_unicode_version(); }
PocketTextConfig pocket_text_config_default(void) {
    PocketTextConfig c = {POCKET_TEXT_MAX_BYTES, 1024U, POCKET_TEXT_PLAIN, 1U, 0U, 0U};
    return c;
}
static int config_valid(const PocketTextConfig *c) {
    return c && c->max_bytes && c->max_bytes <= POCKET_TEXT_MAX_BYTES && c->max_graphemes &&
           c->max_graphemes <= POCKET_TEXT_MAX_BYTES && c->mode >= POCKET_TEXT_PLAIN &&
           c->mode <= POCKET_TEXT_PIN && c->enabled <= 1U && c->read_only <= 1U && c->sensitive <= 1U;
}
static PocketTextStatus field_policy(const PocketTextConfig *c, const char *text, size_t bytes) {
    for (size_t i = 0; i < bytes; ++i) {
        unsigned char x = (unsigned char)text[i];
        /* Native single-line fields. Other Unicode controls are NOT normalized. */
        if (!x || x == '\n' || x == '\r') return POCKET_TEXT_POLICY;
        if (c->mode == POCKET_TEXT_ASCII && (x < 32U || x > 126U)) return POCKET_TEXT_POLICY;
        if (c->mode == POCKET_TEXT_PIN && (x < '0' || x > '9')) return POCKET_TEXT_POLICY;
    }
    return POCKET_TEXT_OK;
}
PocketTextStatus pocket_text_init(PocketTextSession *s, uint64_t field_id,
                                  const PocketTextConfig *config, const char *initial, size_t bytes) {
    if (!s || s->impl || !field_id || !config_valid(config)) return POCKET_TEXT_INVALID_ARGUMENT;
    if (next_session_id == UINT64_MAX) return POCKET_TEXT_EXHAUSTED;
    TextIndex index; PocketTextStatus st = index_text(initial, bytes, &index);
    if (st != POCKET_TEXT_OK) return st;
    if (bytes > config->max_bytes || index.count > config->max_graphemes) return POCKET_TEXT_LIMIT;
    st = field_policy(config, initial, bytes); if (st != POCKET_TEXT_OK) return st;
    TextImpl *p = calloc(1, sizeof(*p)); if (!p) return POCKET_TEXT_EXHAUSTED;
    p->config = *config;
    /* A PIN is always sensitive even if the caller omitted the separate flag. */
    if (p->config.mode == POCKET_TEXT_PIN) p->config.sensitive = 1;
    p->token = (PocketTextToken){field_id, next_session_id++, 0, 1, 1};
    if (bytes) memcpy(p->committed, initial, bytes);
    p->bytes = bytes; p->index = index; p->anchor = p->focus = index.count;
    s->impl = p; return POCKET_TEXT_OK;
}
static void cancel_preedit(TextImpl *p) {
    wipe(p->preedit, sizeof(p->preedit)); p->preedit_bytes = 0; p->composing = 0;
}
void pocket_text_dispose(PocketTextSession *s) {
    TextImpl *p = get(s); if (!p) return;
    wipe(p, sizeof(*p)); free(p); s->impl = NULL;
}
static int counters_available(const TextImpl *p) {
    return p->token.revision != UINT64_MAX && p->token.focus_generation != UINT64_MAX &&
           p->token.engine_generation != UINT64_MAX;
}
PocketTextStatus pocket_text_focus(PocketTextSession *s) {
    TextImpl *p = get(s); if (!p) return POCKET_TEXT_INVALID_ARGUMENT;
    if (!p->config.enabled) return POCKET_TEXT_DISABLED;
    if (!counters_available(p)) return POCKET_TEXT_EXHAUSTED;
    cancel_preedit(p); p->active = 1; ++p->token.focus_generation; ++p->token.revision;
    return POCKET_TEXT_OK;
}
PocketTextStatus pocket_text_blur(PocketTextSession *s) {
    TextImpl *p = get(s); if (!p) return POCKET_TEXT_INVALID_ARGUMENT;
    if (!counters_available(p)) return POCKET_TEXT_EXHAUSTED;
    cancel_preedit(p); p->active = 0; ++p->token.focus_generation; ++p->token.revision;
    return POCKET_TEXT_OK;
}
PocketTextStatus pocket_text_engine_reset(PocketTextSession *s) {
    TextImpl *p = get(s); if (!p) return POCKET_TEXT_INVALID_ARGUMENT;
    if (!counters_available(p)) return POCKET_TEXT_EXHAUSTED;
    cancel_preedit(p); ++p->token.engine_generation; ++p->token.revision;
    return POCKET_TEXT_OK;
}
PocketTextStatus pocket_text_set_flags(PocketTextSession *s, int enabled, int read_only) {
    TextImpl *p = get(s);
    if (!p || (enabled != 0 && enabled != 1) || (read_only != 0 && read_only != 1))
        return POCKET_TEXT_INVALID_ARGUMENT;
    if (!counters_available(p)) return POCKET_TEXT_EXHAUSTED;
    cancel_preedit(p); p->config.enabled = (uint8_t)enabled; p->config.read_only = (uint8_t)read_only;
    if (!enabled) { p->active = 0; ++p->token.focus_generation; }
    ++p->token.revision; return POCKET_TEXT_OK;
}
PocketTextStatus pocket_text_snapshot(const PocketTextSession *s, PocketTextSnapshot *out) {
    const TextImpl *p = get(s); if (!p || !out) return POCKET_TEXT_INVALID_ARGUMENT;
    memset(out, 0, sizeof(*out)); out->token = p->token;
    out->committed_bytes = (uint32_t)p->bytes; out->graphemes = p->index.count;
    out->utf16_units = p->index.utf16[p->index.count]; out->preedit_bytes = (uint32_t)p->preedit_bytes;
    out->anchor_grapheme = p->anchor; out->focus_grapheme = p->focus;
    out->active = p->active; out->composing = p->composing; out->sensitive = p->config.sensitive;
    out->enabled = p->config.enabled; out->read_only = p->config.read_only;
    return POCKET_TEXT_OK;
}
static int token_equal(PocketTextToken a, PocketTextToken b) {
    return a.field_id == b.field_id && a.session_id == b.session_id &&
           a.focus_generation == b.focus_generation && a.engine_generation == b.engine_generation &&
           a.revision == b.revision;
}
static PocketTextStatus replace(TextImpl *p, uint32_t left, uint32_t right,
                                const char *text, size_t bytes) {
    if ((!text && bytes) || left > right || right > p->index.count) return POCKET_TEXT_INVALID_ARGUMENT;
    if (bytes > POCKET_TEXT_MAX_BYTES) return POCKET_TEXT_LIMIT;
    size_t a = p->index.byte[left], b = p->index.byte[right];
    size_t length = p->bytes - (b - a) + bytes;
    if (length > p->config.max_bytes) return POCKET_TEXT_LIMIT;
    char candidate[POCKET_TEXT_MAX_BYTES + 1U] = {0}; TextIndex index;
    memcpy(candidate, p->committed, a);
    if (bytes) memcpy(candidate + a, text, bytes);
    memcpy(candidate + a + bytes, p->committed + b, p->bytes - b);
    PocketTextStatus st = index_text(candidate, length, &index);
    if (st == POCKET_TEXT_OK && index.count > p->config.max_graphemes) st = POCKET_TEXT_LIMIT;
    if (st == POCKET_TEXT_OK) st = field_policy(&p->config, candidate, length);
    if (st == POCKET_TEXT_OK) {
        wipe(p->committed, sizeof(p->committed)); memcpy(p->committed, candidate, length);
        p->bytes = length; p->index = index;
        /* Insertion can merge with either neighboring grapheme. Choose the first
         * complete boundary at/after the inserted suffix, never split a cluster. */
        uint32_t cursor = 0;
        while (cursor < index.count && index.byte[cursor] < a + bytes) ++cursor;
        p->anchor = p->focus = cursor;
    }
    wipe(candidate, sizeof(candidate)); return st;
}
PocketTextStatus pocket_text_apply(PocketTextSession *s, const PocketTextEvent *e, PocketTextEffect *effect) {
    TextImpl *p = get(s);
    if (effect) *effect = POCKET_TEXT_EFFECT_NONE;
    if (!p || !e || !effect || e->version != POCKET_TEXT_API_VERSION ||
        e->action < POCKET_TEXT_INSERT || e->action > POCKET_TEXT_ENTER) return POCKET_TEXT_INVALID_ARGUMENT;
    if (!token_equal(p->token, e->token)) return POCKET_TEXT_STALE_EVENT;
    if (!p->config.enabled) return POCKET_TEXT_DISABLED;
    if (!p->active) return POCKET_TEXT_NOT_FOCUSED;
    if (!counters_available(p)) return POCKET_TEXT_EXHAUSTED;
    int navigation = e->action >= POCKET_TEXT_SELECT && e->action <= POCKET_TEXT_SELECT_ALL;
    if (p->config.read_only && !navigation) return POCKET_TEXT_READ_ONLY;
    int composition_action = e->action >= POCKET_TEXT_COMPOSITION_BEGIN && e->action <= POCKET_TEXT_COMPOSITION_CANCEL;
    if (p->composing && !composition_action && e->action != POCKET_TEXT_ENTER) return POCKET_TEXT_COMPOSING;
    if (composition_action && (p->config.sensitive || p->config.mode != POCKET_TEXT_PLAIN)) return POCKET_TEXT_POLICY;
    if (e->action == POCKET_TEXT_COMPOSITION_BEGIN && p->composing) return POCKET_TEXT_COMPOSING;
    if (e->action > POCKET_TEXT_COMPOSITION_BEGIN && e->action <= POCKET_TEXT_COMPOSITION_CANCEL && !p->composing)
        return POCKET_TEXT_NOT_COMPOSING;
    uint32_t left = p->anchor < p->focus ? p->anchor : p->focus;
    uint32_t right = p->anchor > p->focus ? p->anchor : p->focus;
    PocketTextStatus st = POCKET_TEXT_OK; PocketTextEffect result = POCKET_TEXT_EFFECT_NONE;
    switch (e->action) {
    case POCKET_TEXT_SELECT: {
        uint32_t a, f;
        st = boundary(&p->index, e->unit, e->anchor, &a);
        if (st == POCKET_TEXT_OK) st = boundary(&p->index, e->unit, e->focus, &f);
        if (st == POCKET_TEXT_OK) { p->anchor = a; p->focus = f; }
        break;
    }
    case POCKET_TEXT_LEFT: p->anchor = p->focus = left != right ? left : left ? left - 1U : 0; break;
    case POCKET_TEXT_RIGHT: p->anchor = p->focus = left != right ? right : right < p->index.count ? right + 1U : right; break;
    case POCKET_TEXT_HOME: p->anchor = p->focus = 0; break;
    case POCKET_TEXT_END: p->anchor = p->focus = p->index.count; break;
    case POCKET_TEXT_SELECT_ALL: p->anchor = 0; p->focus = p->index.count; break;
    case POCKET_TEXT_INSERT:
    case POCKET_TEXT_COMPOSITION_COMMIT:
        st = replace(p, left, right, e->text, e->text_bytes);
        if (st == POCKET_TEXT_OK) { cancel_preedit(p); result = POCKET_TEXT_EFFECT_COMMITTED; }
        break;
    case POCKET_TEXT_CLEAR: st = replace(p, 0, p->index.count, NULL, 0); result = POCKET_TEXT_EFFECT_CHANGED; break;
    case POCKET_TEXT_BACKSPACE:
        if (left == right && left) --left;
        st = replace(p, left, right, NULL, 0); result = POCKET_TEXT_EFFECT_CHANGED; break;
    case POCKET_TEXT_DELETE:
        if (left == right && right < p->index.count) ++right;
        st = replace(p, left, right, NULL, 0); result = POCKET_TEXT_EFFECT_CHANGED; break;
    case POCKET_TEXT_COMPOSITION_BEGIN: p->composing = 1; break;
    case POCKET_TEXT_COMPOSITION_UPDATE: {
        if (e->text_bytes > POCKET_TEXT_MAX_PREEDIT_BYTES) return POCKET_TEXT_LIMIT;
        TextIndex index;
        st = index_text(e->text, e->text_bytes, &index);
        if (st == POCKET_TEXT_OK) st = field_policy(&p->config, e->text, e->text_bytes);
        if (st == POCKET_TEXT_OK) {
            wipe(p->preedit, sizeof(p->preedit));
            if (e->text_bytes) memcpy(p->preedit, e->text, e->text_bytes);
            p->preedit_bytes = e->text_bytes;
        }
        break;
    }
    case POCKET_TEXT_COMPOSITION_CANCEL: cancel_preedit(p); break;
    case POCKET_TEXT_ENTER:
        result = p->composing ? POCKET_TEXT_EFFECT_ACCEPT_CANDIDATE : POCKET_TEXT_EFFECT_SUBMIT; break;
    default: return POCKET_TEXT_INVALID_ARGUMENT;
    }
    if (st != POCKET_TEXT_OK) return st;
    ++p->token.revision; *effect = result; return POCKET_TEXT_OK;
}
static PocketTextStatus copy(const char *value, size_t size, char *out, size_t capacity, size_t *bytes) {
    if (!out || !bytes) return POCKET_TEXT_INVALID_ARGUMENT;
    if (capacity <= size) return POCKET_TEXT_LIMIT;
    if (size) memcpy(out, value, size);
    out[size] = 0; *bytes = size; return POCKET_TEXT_OK;
}
PocketTextStatus pocket_text_copy_committed(const PocketTextSession *s, char *out, size_t capacity, size_t *bytes) {
    const TextImpl *p = get(s); if (!p) return POCKET_TEXT_INVALID_ARGUMENT;
    return copy(p->committed, p->bytes, out, capacity, bytes);
}
PocketTextStatus pocket_text_copy_display(const PocketTextSession *s, char *out, size_t capacity, size_t *bytes) {
    const TextImpl *p = get(s); if (!p || !out || !bytes) return POCKET_TEXT_INVALID_ARGUMENT;
    if (!p->config.sensitive) return copy(p->committed, p->bytes, out, capacity, bytes);
    if (capacity <= p->index.count) return POCKET_TEXT_LIMIT;
    memset(out, '*', p->index.count); out[p->index.count] = 0; *bytes = p->index.count;
    return POCKET_TEXT_OK;
}
PocketTextStatus pocket_text_copy_preedit(const PocketTextSession *s, char *out, size_t capacity, size_t *bytes) {
    const TextImpl *p = get(s); if (!p) return POCKET_TEXT_INVALID_ARGUMENT;
    if (p->config.sensitive) return POCKET_TEXT_POLICY;
    return copy(p->preedit, p->preedit_bytes, out, capacity, bytes);
}
