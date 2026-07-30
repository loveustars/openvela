#include "vehicle_gui.h"

#include <stdio.h>
#include <string.h>

void vehicle_gui_init(VehicleGuiState *state)
{
    memset(state->selected_rsu_id, 0, sizeof(state->selected_rsu_id));
    state->selected_from_direction = CARDINAL_UNKNOWN;
    state->selected_turn_direction = TURN_UNKNOWN;
    state->request_enter = false;
    state->request_leave = false;
    state->dirty = true;
}

void vehicle_gui_select_rsu_id(VehicleGuiState *state, const char *rsu_id)
{
    memset(state->selected_rsu_id, 0, sizeof(state->selected_rsu_id));
    if (rsu_id != NULL) {
        strncpy(state->selected_rsu_id, rsu_id, sizeof(state->selected_rsu_id) - 1);
    }
    state->dirty = true;
}

void vehicle_gui_select_from_direction(VehicleGuiState *state, CardinalDirection direction)
{
    state->selected_from_direction = direction;
    state->dirty = true;
}

void vehicle_gui_select_turn_direction(VehicleGuiState *state, TurnDirection direction)
{
    state->selected_turn_direction = direction;
    state->dirty = true;
}

void vehicle_gui_request_enter(VehicleGuiState *state)
{
    state->request_enter = true;
    state->dirty = true;
}

void vehicle_gui_request_leave(VehicleGuiState *state)
{
    state->request_leave = true;
    state->dirty = true;
}

bool vehicle_gui_has_valid_route(const VehicleGuiState *state)
{
    return cardinal_direction_is_valid(state->selected_from_direction) &&
        turn_direction_is_valid(state->selected_turn_direction);
}

bool vehicle_gui_consume_enter_request(VehicleGuiState *state)
{
    bool request = state->request_enter;

    state->request_enter = false;
    return request;
}

bool vehicle_gui_consume_leave_request(VehicleGuiState *state)
{
    bool request = state->request_leave;

    state->request_leave = false;
    return request;
}

const char *vehicle_gui_signal_text(const SpatState *spat_state, char *buffer, size_t buffer_size)
{
    if (buffer == NULL || buffer_size == 0) {
        return "";
    }

    if (spat_state != NULL && spat_state->valid) {
        snprintf(
            buffer,
            buffer_size,
            "%s %s %ds",
            spat_state->light_id,
            spat_state->phase,
            spat_state->remain_s);
    } else {
        snprintf(buffer, buffer_size, "%s", "WAITING");
    }

    return buffer;
}

void vehicle_gui_render(
    VehicleGuiState *state,
    const VehicleState *vehicle,
    const SpatState *spat_state)
{
    char signal_buffer[64];

    if (!state->dirty) {
        return;
    }

    printf(
        "[GUI] RSU=%s FROM=%s TO=%s ENTERED=%s PASSED=%s\n",
        "A+B",
        cardinal_direction_to_string(state->selected_from_direction),
        turn_direction_to_string(state->selected_turn_direction),
        vehicle->has_entered_and_reported ? "YES" : "NO",
        vehicle->has_passed ? "YES" : "NO");

    printf("[GUI] SIGNAL %s\n", vehicle_gui_signal_text(spat_state, signal_buffer, sizeof(signal_buffer)));

    printf("[GUI] BUTTONS FROM[E][S][W][N] TO[LEFT][STRAIGHT][RIGHT] ACTION[ENTER][LEAVE]\n");
    state->dirty = false;
}
