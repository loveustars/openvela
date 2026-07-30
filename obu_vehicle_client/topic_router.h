#ifndef TOPIC_ROUTER_H
#define TOPIC_ROUTER_H

#include <stdbool.h>
#include <stddef.h>

bool topic_router_build_vehicle_state_topic(
    const char *rsu_id,
    char *buffer,
    size_t buffer_size);

bool topic_router_build_vehicle_pass_topic(
    const char *rsu_id,
    char *buffer,
    size_t buffer_size);

#endif
