#ifndef POCKET_IME_SESSION_H
#define POCKET_IME_SESSION_H
#include "pinyin.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef enum { POCKET_INPUT_EN_US, POCKET_INPUT_ZH_CN } PocketInputLocale;
typedef struct { void *impl; } PocketImeSession;
typedef struct {
    PocketInputLocale locale;
    PocketPinyinStatus provider_status;
    uint32_t candidate_count,page,total;
    uint64_t request;
    uint8_t composing,pending;
    int provider_stage,provider_errno;
    unsigned provider_storage;
} PocketImeSnapshot;
/* Borrows an existing TextSession and immutable dictionary until close. Never
 * owns focus, navigates, or submits a form. F6 remains the only focus owner. */
int pocket_ime_open(PocketImeSession *,PocketTextSession *,const void *dictionary,size_t bytes);
PocketTextStatus pocket_ime_locale(PocketImeSession *,PocketTextToken,PocketInputLocale,uint64_t now);
PocketTextStatus pocket_ime_key(PocketImeSession *,PocketTextToken,char ascii,uint64_t now);
PocketTextStatus pocket_ime_backspace(PocketImeSession *,PocketTextToken,uint64_t now);
PocketTextStatus pocket_ime_page(PocketImeSession *,PocketTextToken,uint32_t page,uint64_t now);
PocketTextStatus pocket_ime_choose(PocketImeSession *,PocketTextToken,uint64_t request,uint32_t index,uint64_t now);
/* Returns SUBMIT only for a distinct Enter outside composition. */
PocketTextStatus pocket_ime_enter(PocketImeSession *,PocketTextToken,uint64_t now,PocketTextEffect *);
PocketTextStatus pocket_ime_cancel(PocketImeSession *,PocketTextToken);
PocketPinyinStatus pocket_ime_step(PocketImeSession *,uint64_t now);
int pocket_ime_snapshot(const PocketImeSession *,PocketImeSnapshot *);
/* Explicit rendering access. Not a log/trace/screenshot API. */
int pocket_ime_candidates(const PocketImeSession *,PocketPinyinResult *);
void pocket_ime_close(PocketImeSession *);
#ifdef __cplusplus
}
#endif
#endif
