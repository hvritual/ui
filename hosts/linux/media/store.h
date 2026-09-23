#ifndef COFFEE_MEDIA_STORE_H
#define COFFEE_MEDIA_STORE_H
#include "host.h"
#define MEDIA_PACKET_BYTES (64U+8U*256U*128U*4U)
typedef struct {
 const char *root;
 char applied[65], rejected[65];
 unsigned long applied_count, rejected_count, deferred_count;
} MediaStore;
int media_packet_valid(const unsigned char *bytes, size_t size);
/* Poll only at a frame boundary. Files are installed by trusted mediactl. */
int media_store_poll(MediaStore *store);
int media_builtin(const char *assets);
#endif
