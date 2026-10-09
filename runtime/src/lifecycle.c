#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "lifecycle.h"

#include <string.h>

const char *minibox_state_to_string(minibox_state_t state) {
    switch (state) {
        case MINIBOX_STATE_CREATED:    return "CREATED";
        case MINIBOX_STATE_STARTING:   return "STARTING";
        case MINIBOX_STATE_RUNNING:    return "RUNNING";
        case MINIBOX_STATE_STOPPING:   return "STOPPING";
        case MINIBOX_STATE_COMPLETED:  return "COMPLETED";
        case MINIBOX_STATE_TERMINATED: return "TERMINATED";
        case MINIBOX_STATE_FAILED:     return "FAILED";
        default:                       return "UNKNOWN";
    }
}

minibox_state_t minibox_state_from_string(const char *str) {
    if (!str) return MINIBOX_STATE_UNKNOWN;
    if (strcmp(str, "CREATED") == 0)    return MINIBOX_STATE_CREATED;
    if (strcmp(str, "STARTING") == 0)   return MINIBOX_STATE_STARTING;
    if (strcmp(str, "RUNNING") == 0)    return MINIBOX_STATE_RUNNING;
    if (strcmp(str, "STOPPING") == 0)   return MINIBOX_STATE_STOPPING;
    if (strcmp(str, "COMPLETED") == 0)  return MINIBOX_STATE_COMPLETED;
    if (strcmp(str, "TERMINATED") == 0) return MINIBOX_STATE_TERMINATED;
    if (strcmp(str, "FAILED") == 0)     return MINIBOX_STATE_FAILED;
    return MINIBOX_STATE_UNKNOWN;
}

bool minibox_is_valid_transition(minibox_state_t from, minibox_state_t to) {
    if (from == to) return true;

    switch (from) {
        case MINIBOX_STATE_CREATED:
            return (to == MINIBOX_STATE_STARTING || to == MINIBOX_STATE_FAILED);

        case MINIBOX_STATE_STARTING:
            return (to == MINIBOX_STATE_RUNNING || to == MINIBOX_STATE_FAILED);

        case MINIBOX_STATE_RUNNING:
            return (to == MINIBOX_STATE_STOPPING || to == MINIBOX_STATE_COMPLETED || to == MINIBOX_STATE_TERMINATED);

        case MINIBOX_STATE_STOPPING:
            return (to == MINIBOX_STATE_TERMINATED || to == MINIBOX_STATE_COMPLETED);

        case MINIBOX_STATE_COMPLETED:
        case MINIBOX_STATE_TERMINATED:
        case MINIBOX_STATE_FAILED:
            return false; /* Terminal states cannot transition */

        default:
            return false;
    }
}

bool minibox_state_is_terminal(minibox_state_t state) {
    return (state == MINIBOX_STATE_COMPLETED ||
            state == MINIBOX_STATE_TERMINATED ||
            state == MINIBOX_STATE_FAILED);
}
