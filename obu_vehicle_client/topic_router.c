#include "topic_router.h"

#include <stdio.h>

static bool build_topic(
    const char *prefix,
    const char *rsu_id,
    char *buffer,
    size_t buffer_size)
{
    int written;

    if (prefix == NULL || rsu_id == NULL || buffer == NULL || buffer_size == 0) {
        return false;
    }

    written = snprintf(buffer, buffer_size, "%s/%s", prefix, rsu_id);
    return written > 0 && (size_t)written < buffer_size;
}

bool topic_router_build_vehicle_state_topic(
    const char *rsu_id,
    char *buffer,
    size_t buffer_size)
{
    return build_topic("v2i/vehicle/state", rsu_id, buffer, buffer_size);
}

bool topic_router_build_vehicle_pass_topic(
    const char *rsu_id,
    char *buffer,
    size_t buffer_size)
{
    return build_topic("v2i/vehicle/pass", rsu_id, buffer, buffer_size);
}
