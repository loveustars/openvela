#include "spat_state.h"

#include <string.h>

static bool spat_state_is_valid_phase(const char *phase)
{
    return strcmp(phase, "green") == 0
        || strcmp(phase, "yellow") == 0
        || strcmp(phase, "red") == 0
        || strcmp(phase, "NS_STRAIGHT") == 0
        || strcmp(phase, "NS_LEFT") == 0
        || strcmp(phase, "EW_STRAIGHT") == 0
        || strcmp(phase, "EW_LEFT") == 0
        || strcmp(phase, "YELLOW") == 0
        || strcmp(phase, "ALL_RED") == 0;
}

void spat_state_init(SpatState *state)
{
    if (state == NULL) {
        return;
    }

    memset(state, 0, sizeof(*state));
}

bool spat_state_update(
    SpatState *state,
    const char *light_id,
    const char *phase,
    int remain_s,
    unsigned long timestamp)
{
    if (state == NULL || light_id == NULL || phase == NULL) {
        return false;
    }

    if (light_id[0] == '\0' || !spat_state_is_valid_phase(phase) || remain_s < 0) {
        return false;
    }

    memset(state->light_id, 0, sizeof(state->light_id));
    memset(state->phase, 0, sizeof(state->phase));
    strncpy(state->light_id, light_id, sizeof(state->light_id) - 1);
    strncpy(state->phase, phase, sizeof(state->phase) - 1);

    state->remain_s = remain_s;
    state->timestamp = timestamp;
    state->valid = true;
    state->updated = true;
    return true;
}

void spat_state_mark_consumed(SpatState *state)
{
    if (state == NULL) {
        return;
    }

    state->updated = false;
}
