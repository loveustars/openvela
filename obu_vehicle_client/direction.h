#ifndef DIRECTION_H
#define DIRECTION_H

#include <stdbool.h>

typedef enum {
    CARDINAL_UNKNOWN = 0,
    CARDINAL_EAST,
    CARDINAL_SOUTH,
    CARDINAL_WEST,
    CARDINAL_NORTH
} CardinalDirection;

typedef enum {
    TURN_UNKNOWN = 0,
    TURN_LEFT,
    TURN_STRAIGHT,
    TURN_RIGHT
} TurnDirection;

const char *cardinal_direction_to_string(CardinalDirection direction);
const char *turn_direction_to_string(TurnDirection direction);
bool cardinal_direction_is_valid(CardinalDirection direction);
bool turn_direction_is_valid(TurnDirection direction);

#endif
