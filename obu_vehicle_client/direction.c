#include "direction.h"

const char *cardinal_direction_to_string(CardinalDirection direction)
{
    switch (direction) {
    case CARDINAL_EAST:
        return "E";
    case CARDINAL_SOUTH:
        return "S";
    case CARDINAL_WEST:
        return "W";
    case CARDINAL_NORTH:
        return "N";
    case CARDINAL_UNKNOWN:
    default:
        return "unknown";
    }
}

const char *turn_direction_to_string(TurnDirection direction)
{
    switch (direction) {
    case TURN_LEFT:
        return "left";
    case TURN_STRAIGHT:
        return "straight";
    case TURN_RIGHT:
        return "right";
    case TURN_UNKNOWN:
    default:
        return "unknown";
    }
}

bool cardinal_direction_is_valid(CardinalDirection direction)
{
    return direction == CARDINAL_EAST ||
        direction == CARDINAL_SOUTH ||
        direction == CARDINAL_WEST ||
        direction == CARDINAL_NORTH;
}

bool turn_direction_is_valid(TurnDirection direction)
{
    return direction == TURN_LEFT ||
        direction == TURN_STRAIGHT ||
        direction == TURN_RIGHT;
}
