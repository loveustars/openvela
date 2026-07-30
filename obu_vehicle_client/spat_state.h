#ifndef SPAT_STATE_H
#define SPAT_STATE_H

#include <stdbool.h>

typedef struct {
    char light_id[16];
    char phase[16];
    int remain_s;
    unsigned long timestamp;
    bool valid;
    bool updated;
} SpatState;

void spat_state_init(SpatState *state);
bool spat_state_update(
    SpatState *state,
    const char *light_id,
    const char *phase,
    int remain_s,
    unsigned long timestamp);
void spat_state_mark_consumed(SpatState *state);

#endif
