#ifndef JSON_BUILDER_H
#define JSON_BUILDER_H

#include <stdbool.h>
#include <stddef.h>

#include "vehicle_state.h"

bool json_builder_build_vehicle_state(
    const VehicleState *state,
    char *buffer,
    size_t buffer_size);

bool json_builder_build_vehicle_pass(
    const VehicleState *state,
    char *buffer,
    size_t buffer_size);

#endif
