#ifndef POCKET_APPLICATION_PACKAGE_H
#define POCKET_APPLICATION_PACKAGE_H
#include <stddef.h>
#include <stdint.h>

/* Experimental container contract. No extraction, execution or authentication. */
#define PUI_FORMAT_VERSION 1u
#define PUI_RUNTIME_API 1u
#define PUI_SDK_API 1u
#define PUI_HEADER_SIZE 192u
#define PUI_ENTRY_SIZE 96u
#define PUI_MAX_FILES 16u
#define PUI_MAX_BYTES (16u * 1024u * 1024u)
#define PUI_MAX_HEAP (8u * 1024u * 1024u)
#define PUI_TARGET_600 1u
#define PUI_TARGET_800 2u
#define PUI_CAP_CORE 1u
#define PUI_CAP_ASCII_KEYBOARD 2u
#define PUI_CAP_IMAGES 4u
#define PUI_CAP_ALL 7u

typedef enum {
    PUI_OK, PUI_ARGUMENT, PUI_TRUNCATED, PUI_FORMAT, PUI_VERSION,
    PUI_TARGET, PUI_CAPABILITY, PUI_BUDGET, PUI_UNSIGNED,
    PUI_INTEGRITY, PUI_FILE_TABLE, PUI_REQUIRED_FILE
} PuiStatus;

typedef struct {
    uint32_t runtime_api, sdk_api, target, capabilities;
    uint32_t max_heap_bytes, max_asset_bytes;
    int allow_unsigned;
} PuiPolicy;

typedef struct {
    char name[48];
    const unsigned char *data;
    size_t length;
} PuiFile;

typedef struct {
    char app_id[64], app_version[32];
    uint32_t runtime_min, runtime_max, sdk_api, targets, capabilities;
    uint32_t heap_bytes, asset_bytes, file_count;
    PuiFile files[PUI_MAX_FILES];
} PuiPackage;

/* Digest utility for deterministic container integrity, NOT a signature. */
void pui_sha256(const void *bytes, size_t length, unsigned char digest[32]);
/* Output is zeroed on failure. Borrowed file views remain valid only while the
 * original input buffer is alive AND immutable. No file/system call is made.
 * Requested budgets are checked here; an execution host must enforce them. */
PuiStatus pui_validate(const void *bytes, size_t length,
                       const PuiPolicy *policy, PuiPackage *out);
const char *pui_status_name(PuiStatus status);
#endif
