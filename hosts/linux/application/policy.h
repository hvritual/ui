#ifndef POCKET_APPLICATION_POLICY_H
#define POCKET_APPLICATION_POLICY_H
#include <stddef.h>
#include <stdint.h>
/* Experimental native host capabilities. No third-party engine handles/API. */
#define POCKET_APP_CAP_CORE 1u
#define POCKET_APP_CAP_ASCII_KEYBOARD 2u
#define POCKET_APP_CAP_IMAGES 4u
#define POCKET_APP_CAP_PINYIN 8u
#define POCKET_APP_CAP_ALL 15u
#define POCKET_APP_CAP_LEGACY 7u
#define POCKET_APP_MAX_HEAP (8u * 1024u * 1024u)
typedef struct {
    size_t heap_bytes; /* managed application QuickJS heap, not total process RSS */
    uint32_t capabilities;
} PocketApplicationPolicy;
#endif
