#ifndef POCKET_PINYIN_H
#define POCKET_PINYIN_H
#include <stddef.h>
#include <stdint.h>
#include "../text-input/session.h"
#ifdef __cplusplus
extern "C" {
#endif
#define POCKET_PINYIN_MAX_INPUT 32U
#define POCKET_PINYIN_PAGE_SIZE 5U
#define POCKET_PINYIN_MAX_TEXT 64U
#define POCKET_PINYIN_TIMEOUT_MS 2000U
/* This is an internal same-binary worker protocol, not external device IPC. */
typedef enum {
    POCKET_PINYIN_OK, POCKET_PINYIN_PENDING, POCKET_PINYIN_INVALID,
    POCKET_PINYIN_UNAVAILABLE, POCKET_PINYIN_TIMEOUT, POCKET_PINYIN_EXHAUSTED
} PocketPinyinStatus;
typedef struct { char text[POCKET_PINYIN_MAX_TEXT]; uint32_t consumed_bytes; } PocketPinyinCandidate;
typedef struct {
    uint64_t request;
    PocketTextToken token;
    PocketPinyinStatus status;
    uint32_t page, total, count, decoded_bytes;
    PocketPinyinCandidate candidates[POCKET_PINYIN_PAGE_SIZE];
} PocketPinyinResult;
typedef struct { void *impl; int diagnostic_stage, diagnostic_errno; unsigned storage_mode; } PocketPinyin;
enum { POCKET_PINYIN_STAGE_NONE=0, POCKET_PINYIN_STAGE_DICTIONARY=1, POCKET_PINYIN_STAGE_MEMFD=2,
       POCKET_PINYIN_STAGE_TEMPFILE=3, POCKET_PINYIN_STAGE_SEAL=4, POCKET_PINYIN_STAGE_VERIFY=5,
       POCKET_PINYIN_STAGE_SOCKET=6, POCKET_PINYIN_STAGE_FD_ENUM=7, POCKET_PINYIN_STAGE_FORK=8,
       POCKET_PINYIN_STAGE_DECODER=9 };
enum { POCKET_PINYIN_STORAGE_NONE=0, POCKET_PINYIN_STORAGE_MEMFD=1, POCKET_PINYIN_STORAGE_UNLINKED_FILE=2 };
/* The caller is the single UI owner, before starting threads. Dictionary bytes
 * are copied and pinned; no user dictionary, filesystem learning, or network.
 * Decoding runs in an owned child, not on the UI thread. This is not a sandbox. */
/* Structural package admission uses the pinned dictionary identity without forking. */
int pocket_pinyin_dictionary_valid(const void *,size_t);
void pocket_pinyin_diagnostics(const PocketPinyin *,int *stage,int *system_errno,unsigned *storage_mode);
PocketPinyinStatus pocket_pinyin_open(PocketPinyin *,const void *dictionary,size_t bytes,uint64_t now_ms);
/* At most one in-flight + one newest queued request. Replaced text is erased. */
PocketPinyinStatus pocket_pinyin_request(PocketPinyin *,PocketTextToken,const char *,size_t,
                                        uint32_t page,uint64_t now_ms,uint64_t *request);
PocketPinyinStatus pocket_pinyin_poll(PocketPinyin *,uint64_t now_ms,PocketPinyinResult *);
void pocket_pinyin_cancel(PocketPinyin *);
void pocket_pinyin_close(PocketPinyin *);
#ifdef __cplusplus
}
#endif
#endif
