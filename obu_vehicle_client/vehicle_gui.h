#ifndef VEHICLE_GUI_H
#define VEHICLE_GUI_H

#include <stdbool.h>
#include <stddef.h>

#include "config.h"
#include "direction.h"
#include "spat_state.h"
#include "vehicle_state.h"

typedef struct {
    char selected_rsu_id[RSU_ID_MAX_LEN];
    CardinalDirection selected_from_direction;
    TurnDirection selected_turn_direction;
    bool request_enter;
    bool request_leave;
    bool dirty;
} VehicleGuiState;

void vehicle_gui_init(VehicleGuiState *state);
void vehicle_gui_select_rsu_id(VehicleGuiState *state, const char *rsu_id);
void vehicle_gui_select_from_direction(VehicleGuiState *state, CardinalDirection direction);
void vehicle_gui_select_turn_direction(VehicleGuiState *state, TurnDirection direction);
void vehicle_gui_request_enter(VehicleGuiState *state);
void vehicle_gui_request_leave(VehicleGuiState *state);
bool vehicle_gui_has_valid_route(const VehicleGuiState *state);
bool vehicle_gui_consume_enter_request(VehicleGuiState *state);
bool vehicle_gui_consume_leave_request(VehicleGuiState *state);
void vehicle_gui_render(
    VehicleGuiState *state,
    const VehicleState *vehicle,
    const SpatState *spat_state);
const char *vehicle_gui_signal_text(const SpatState *spat_state, char *buffer, size_t buffer_size);

#endif
