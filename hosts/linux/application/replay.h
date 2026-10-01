#ifndef POCKET_APPLICATION_REPLAY_H
#define POCKET_APPLICATION_REPLAY_H
#include "../framework.h"
/* Explicit headless-only diagnostic input; never a physical-board claim.
 * Each line is: milliseconds phase pointer x y. Phase 0=tick, 1=down,
 * 2=move, 3=up, 4=backend cancel. Only synthetic fixtures belong in evidence. */
int pocket_framework_replay(PocketFramework *runtime, const char *path,
                            uint64_t start_ns, unsigned *sample_count);
#endif
