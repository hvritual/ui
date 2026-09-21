#include "host.h"
#include "display/cli.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv) {
    if (argc > 1 && (!strcmp(argv[1], "--probe-display") || !strcmp(argv[1], "--display-test")))
        return display_cli(argc, argv);
    LinuxHost host = {0};
    const char *profile = NULL, *root = NULL, *bundle = NULL, *pack = NULL;
    unsigned long limit = 60;
    uint64_t now;
    for (int i = 1; i < argc; i += 2) {
        if (i + 1 == argc) { fprintf(stderr, "HOST_ARGUMENT_MISSING\n"); return 2; }
        if (!strcmp(argv[i], "--profile")) profile = argv[i+1];
        else if (!strcmp(argv[i], "--asset-root")) root = argv[i+1];
        else if (!strcmp(argv[i], "--bundle")) bundle = argv[i+1];
        else if (!strcmp(argv[i], "--pack")) pack = argv[i+1];
        else if (!strcmp(argv[i], "--ticks")) {
            char *end; errno = 0; limit = strtoul(argv[i+1], &end, 10);
            if (errno || *end || limit == 0 || limit > 600) return 2;
        } else { fprintf(stderr, "HOST_ARGUMENT_UNKNOWN\n"); return 2; }
    }
    if (!profile || !root || !bundle || !host_monotonic_ns(&now)) return 2;
    if (!host_open(&host, profile, root, bundle, pack, now)) goto failure;
    while (host.turns < limit) {
        if (!host_sleep_until(host.clock.next_ns) || !host_monotonic_ns(&now) || host_pump(&host, now) < 0) goto failure;
    }
    printf("HOST_OK mode=headless width=%u height=%u turns=%llu renders=%llu overruns=%llu hardware_tested=false\n",
           host.width, host.height, (unsigned long long)host.turns,
           (unsigned long long)host.renders, (unsigned long long)host.clock.overruns);
    host_close(&host); return 0;
failure:
    fprintf(stderr, "%s\n", host.error ? host.error : "HOST_CLOCK_FAILED");
    host_close(&host); return 1;
}
