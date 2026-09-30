#ifndef POCKET_TEST_APPLICATION_COMPAT_H
#define POCKET_TEST_APPLICATION_COMPAT_H
/* Keep the frozen pre-separation workload's assertions unchanged while routing
 * it to the application payload. This header is never included by runtime C. */
#include "hosts/linux/application/application.h"
#define CoffeeApp PocketApplication
#define CoffeeAppStats PocketApplicationStats
#define coffee_app_dispose pocket_application_close
#define coffee_app_step pocket_application_step
#define coffee_app_scene pocket_application_scene
#define coffee_app_stats pocket_application_stats
#define coffee_app_interaction pocket_application_interaction
#define coffee_app_keyboard_snapshot pocket_application_keyboard_snapshot
#define coffee_app_snapshot_allowed pocket_application_snapshot_allowed
#endif
