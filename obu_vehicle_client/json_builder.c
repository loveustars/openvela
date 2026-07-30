#include "json_builder.h"

#include <stdbool.h>
#include <stdio.h>

#include "direction.h"

bool json_builder_build_vehicle_state(
    const VehicleState *state,
    char *buffer,
    size_t buffer_size)
{
    int written = snprintf(
        buffer,
        buffer_size,
        "{"
        "\"vehicle_id\":\"%s\","
        "\"from_direction\":\"%s\","
        "\"to_direction\":\"%s\","
        "\"status\":\"entering\","
        "\"timestamp\":%lu"
        "}",
        state->vehicle_id,
        cardinal_direction_to_string(state->from_direction),
        turn_direction_to_string(state->turn_direction),
        state->timestamp);

    return written > 0 && (size_t)written < buffer_size;
}

bool json_builder_build_vehicle_pass(
    const VehicleState *state,
    char *buffer,
    size_t buffer_size)
{
    int written = snprintf(
        buffer,
        buffer_size,
        "{"
        "\"vehicle_id\":\"%s\","
        "\"from_direction\":\"%s\","
        "\"to_direction\":\"%s\","
        "\"status\":\"passed\","
        "\"timestamp\":%lu"
        "}",
        state->vehicle_id,
        cardinal_direction_to_string(state->from_direction),
        turn_direction_to_string(state->turn_direction),
        state->timestamp);

    return written > 0 && (size_t)written < buffer_size;
}
