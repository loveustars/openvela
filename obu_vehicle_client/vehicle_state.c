#include "vehicle_state.h"

#include <string.h>

#include "config.h"

void vehicle_state_init(VehicleState *state)
{
    memset(state, 0, sizeof(*state));

    strncpy(state->vehicle_id, OBU_VEHICLE_ID, sizeof(state->vehicle_id) - 1);
    state->timestamp = 0;
    state->entered_at_ms = 0;
    state->from_direction = CARDINAL_UNKNOWN;
    state->turn_direction = TURN_UNKNOWN;
    state->has_entered_and_reported = false;
    state->has_passed = false;
}

void vehicle_state_update_timestamp(VehicleState *state, unsigned long timestamp)
{
    state->timestamp = timestamp;
}

void vehicle_state_set_route(
    VehicleState *state,
    CardinalDirection from_direction,
    TurnDirection turn_direction)
{
    state->from_direction = from_direction;
    state->turn_direction = turn_direction;
}

void vehicle_state_set_rsu_id(VehicleState *state, const char *rsu_id)
{
    memset(state->selected_rsu_id, 0, sizeof(state->selected_rsu_id));
    if (rsu_id != NULL) {
        strncpy(state->selected_rsu_id, rsu_id, sizeof(state->selected_rsu_id) - 1);
    }
}
