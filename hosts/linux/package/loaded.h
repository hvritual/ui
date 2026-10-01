#ifndef POCKET_LOADED_PACKAGE_H
#define POCKET_LOADED_PACKAGE_H
#include "package.h"

/* Single-owner-thread reference. Do not copy this handle by assignment; retain
 * it explicitly. An application/runtime may retain it past the caller's close. */
typedef struct { void *impl; } PuiLoadedPackage;
typedef struct {
    size_t container_bytes;
    uint32_t capabilities, heap_bytes, asset_bytes;
    char sha256[65];
} PuiLoadedSnapshot;

PuiPolicy pui_runtime_policy(unsigned height, int allow_unsigned);
/* Read once from a bounded regular file into anonymous private memory, seal it
 * read-only, then verify it. No extraction, file-backed mmap or later reopen.
 * Failure leaves an empty handle; error is a fixed code, never package text. */
int pui_loaded_open(PuiLoadedPackage *out, const char *path,
                    const PuiPolicy *policy, const char **error);
int pui_loaded_retain(PuiLoadedPackage *out, const PuiLoadedPackage *source);
int pui_loaded_close(PuiLoadedPackage *package);
const PuiPackage *pui_loaded_manifest(const PuiLoadedPackage *package);
const PuiFile *pui_loaded_file(const PuiLoadedPackage *package, const char *name);
int pui_loaded_snapshot(const PuiLoadedPackage *package, PuiLoadedSnapshot *out);
#endif
