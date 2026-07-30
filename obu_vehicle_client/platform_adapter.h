#ifndef PLATFORM_ADAPTER_H
#define PLATFORM_ADAPTER_H

#include <stdbool.h>

#include "direction.h"
#include "spat_state.h"
#include "vehicle_gui.h"
#include "vehicle_state.h"

typedef struct {
    bool select_rsu_id;
    char rsu_id[RSU_ID_MAX_LEN];
    bool select_from_direction;
    CardinalDirection from_direction;
    bool select_turn_direction;
    TurnDirection turn_direction;
    bool press_enter;
    bool press_leave;
} GuiInputEvent;

bool platform_adapter_init(void);
bool platform_adapter_gui_init(
    VehicleGuiState *gui_state,
    VehicleState *vehicle_state,
    SpatState *spat_state);
bool platform_adapter_network_ready(void);
unsigned long platform_adapter_get_time_ms(void);
unsigned long platform_adapter_get_unix_timestamp(void);
void platform_adapter_sleep_ms(unsigned long delay_ms);
GuiInputEvent platform_adapter_read_gui_input(unsigned long now_ms);
bool platform_adapter_mock_receive_spat(unsigned long now_ms, SpatState *spat_state);
void platform_adapter_gui_refresh(void);
bool platform_adapter_should_quit(void);
void platform_adapter_shutdown(void);

#endif
