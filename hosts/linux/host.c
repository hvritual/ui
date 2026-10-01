#include "host.h"
#include <stdlib.h>
#include <string.h>
#include "profiles.generated.h"

static LinuxHost *owner;
static int fail(LinuxHost *h, const char *code) {
    h->error = code; h->state = HOST_FAILED; return 0;
}
void host_close(LinuxHost *h) {
    if (!h) return;
    if (owner == h) { pocket_runtime_shutdown(); owner = NULL; }
    /* The runtime must release its borrowed ArrayBuffer before this free. */
    host_asset_free(&h->pack); host_asset_free(&h->script);
    h->state = HOST_STOPPED;
}
static int open_host(LinuxHost *h, const char *profile, const char *root,
                     const char *bundle, const char *source, size_t length,
                     const char *pack, const unsigned char *pack_bytes, size_t pack_length, uint64_t now) {
    size_t i;
    if (!h || !profile || owner || h->state != HOST_STOPPED || h->script.data || h->pack.data) return 0;
    memset(h, 0, sizeof(*h));
    for (i = 0; i < HOST_PROFILE_COUNT; ++i) {
        if (!strcmp(profile, host_profiles[i].id)) {
            h->width = host_profiles[i].width; h->height = host_profiles[i].height; break;
        }
    }
    if (!h->width || now > UINT64_MAX - 1000000000ULL) return fail(h, "HOST_PROFILE_OR_CLOCK_INVALID");
    int script_ok = 0;
    if (bundle) script_ok = host_asset_read(root, bundle, 1024 * 1024, &h->script);
    else if (source && length && length <= 1024 * 1024) {
        h->script.data = malloc(length + 1);
        if (h->script.data) {
            memcpy(h->script.data, source, length); h->script.data[length] = 0;
            h->script.length = length; script_ok = 1;
        }
    }
    int pack_ok = 1;
    if (pack_bytes) {
        pack_ok = 0;
        if (!pack && pack_length && pack_length <= 16u * 1024u * 1024u) {
            h->pack.data = malloc(pack_length);
            if (h->pack.data) { memcpy(h->pack.data, pack_bytes, pack_length); h->pack.length = pack_length; pack_ok = 1; }
        }
    } else if (pack) pack_ok = host_asset_read(root, pack, 16u * 1024u * 1024u, &h->pack);
    else if (pack_length) pack_ok = 0;
    if (!script_ok || memchr(h->script.data, 0, h->script.length) || !pack_ok) {
        host_close(h); return fail(h, "HOST_ASSET_REJECTED");
    }
    owner = h;
    if (!pocket_runtime_boot((const char *)h->script.data, h->script.length,
                             h->pack.data, h->pack.length, (int)h->width, (int)h->height)) {
        /* Do not put guest exception text, input text or file contents in logs. */
        host_close(h); return fail(h, "HOST_BOOT_FAILED");
    }
    host_clock_start(&h->clock, now); h->state = HOST_RUNNING; return 1;
}
int host_open(LinuxHost *h, const char *profile, const char *root,
              const char *bundle, const char *pack, uint64_t now) {
    return open_host(h, profile, root, bundle, NULL, 0, pack, NULL, 0, now);
}
int host_open_source(LinuxHost *h, const char *profile, const char *root,
                     const char *source, size_t length, const char *pack, uint64_t now) {
    return open_host(h, profile, root, NULL, source, length, pack, NULL, 0, now);
}
int host_open_buffers(LinuxHost *h, const char *profile, const char *source, size_t length,
                      const unsigned char *pack_bytes, size_t pack_length, uint64_t now) {
    if (!pack_bytes || !pack_length) return 0;
    return open_host(h, profile, NULL, NULL, source, length, NULL, pack_bytes, pack_length, now);
}
int host_turn(LinuxHost *h, const PocketRuntimeInput *input) {
    const PocketRuntimeInput empty = {0};
    if (!h || owner != h || h->state != HOST_RUNNING) return 0;
    if (!pocket_runtime_tick(input ? input : &empty)) return fail(h, "HOST_GUEST_TURN_FAILED");
    ++h->turns; return 1;
}
int host_turn_contacts(LinuxHost *h, const PocketRuntimeContactsInput *input) {
    const PocketRuntimeContactsInput empty = {0};
    if (!h || owner != h || h->state != HOST_RUNNING) return 0;
    if (!pocket_runtime_tick_contacts(input ? input : &empty))
        return fail(h, "HOST_GUEST_TURN_FAILED");
    ++h->turns; return 1;
}
int host_render(LinuxHost *h, HostFrame *frame) {
    if (!h || owner != h || h->state != HOST_RUNNING || !frame) return 0;
    memset(frame, 0, sizeof(*frame));
    frame->pixels = pocket_runtime_render(); frame->width = pocket_runtime_width();
    frame->height = pocket_runtime_height(); frame->stride = pocket_runtime_stride();
    frame->length = pocket_runtime_length();
    if (!frame->pixels || frame->width != h->width || frame->height != h->height ||
        frame->stride != h->width * 4 || frame->length != (size_t)frame->stride * frame->height)
        return fail(h, "HOST_RENDER_CONTRACT_FAILED");
    ++h->renders; return 1;
}
int host_present_latest(LinuxHost *h, const HostFrame *frame, int force,
                        HostPresenter present, void *context) {
    int bounds[4];
    if (!h || owner != h || h->state != HOST_RUNNING || !frame || !frame->pixels ||
        frame->width != h->width || frame->height != h->height || !present) return 0;
    /* Require both indicators to agree. Inconsistent damage falls back to full
       presentation, never a silent dropped update. Initial/resumed destinations
       and explicit recovery remain full presentations even with no damage. */
    if (!force && h->presentation_valid && !pocket_runtime_damage_pixels() &&
        !pocket_runtime_damage_bounds(bounds)) {
        ++h->clean_frames_skipped;
        return 1;
    }
    if (!present(context, frame)) return fail(h, "HOST_PRESENT_FAILED");
    h->presentation_valid = 1;
    ++h->presented_frames;
    return 1;
}
int host_pump_present(LinuxHost *h, uint64_t now, HostPresenter present, void *context) {
    HostFrame frame;
    int due;
    if (!h || owner != h || (h->state != HOST_RUNNING && h->state != HOST_PAUSED)) return -1;
    due = host_clock_due(&h->clock, now);
    if (due < 0) { fail(h, "HOST_CLOCK_REVERSED"); return -1; }
    for (int i = 0; i < due; ++i) {
        if (!host_turn(h, NULL)) return -1;
        /* 60 guest/core turns; at most 30 offscreen renders per logical second. */
        if ((h->turns % 2) == 0) {
            if (!host_render(h, &frame)) return -1;
            if (present && !present(context, &frame)) { fail(h, "HOST_PRESENT_FAILED"); return -1; }
        }
    }
    return due;
}
int host_pump_present_contacts(LinuxHost *h, uint64_t now,
                               HostContactsSource source, void *source_context,
                               HostPresenter present, void *present_context) {
    HostFrame frame;
    int due;
    if (!h || owner != h || (h->state != HOST_RUNNING && h->state != HOST_PAUSED)) return -1;
    due = host_clock_due(&h->clock, now);
    if (due < 0) { fail(h, "HOST_CLOCK_REVERSED"); return -1; }
    for (int i = 0; i < due; ++i) {
        PocketRuntimeContactsInput input = {0};
        if (source && !source(source_context, &input)) {
            fail(h, "HOST_INPUT_SAMPLE_FAILED");
            return -1;
        }
        if (!host_turn_contacts(h, &input)) return -1;
        if ((h->turns % 2) == 0) {
            if (!host_render(h, &frame)) return -1;
            if (present && !present(present_context, &frame)) {
                fail(h, "HOST_PRESENT_FAILED");
                return -1;
            }
        }
    }
    return due;
}
int host_pump(LinuxHost *h, uint64_t now) {
    return host_pump_present(h, now, NULL, NULL);
}
int host_pause(LinuxHost *h, int paused, uint64_t now) {
    if (!h || owner != h || (h->state != HOST_RUNNING && h->state != HOST_PAUSED)) return 0;
    if (now < h->clock.last_ns || now > UINT64_MAX - 1000000000ULL) return fail(h, "HOST_CLOCK_REVERSED");
    host_clock_pause(&h->clock, paused, now); h->state = paused ? HOST_PAUSED : HOST_RUNNING;
    h->presentation_valid = 0; return 1;
}
