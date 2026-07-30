#ifndef VEHICLE_STATE_H
#define VEHICLE_STATE_H

#include <stdbool.h>

#include "direction.h"

typedef struct {
    char vehicle_id[16];
    char selected_rsu_id[16];
    unsigned long timestamp;
    unsigned long entered_at_ms;
    CardinalDirection from_direction;
    TurnDirection turn_direction;
    bool has_entered_and_reported;
    bool has_passed;
} VehicleState;

void vehicle_state_init(VehicleState *state);
void vehicle_state_update_timestamp(VehicleState *state, unsigned long timestamp);
void vehicle_state_set_route(
    VehicleState *state,
    CardinalDirection from_direction,
    TurnDirection turn_direction);
void vehicle_state_set_rsu_id(VehicleState *state, const char *rsu_id);

#endif
