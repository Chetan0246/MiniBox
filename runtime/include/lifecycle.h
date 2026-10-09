#ifndef MINIBOX_LIFECYCLE_H
#define MINIBOX_LIFECYCLE_H

#include <stdbool.h>

/**
 * Workload lifecycle states as defined in the MiniBox architecture.
 */
typedef enum {
    MINIBOX_STATE_UNKNOWN = 0,
    MINIBOX_STATE_CREATED,      /* Workload record initialized */
    MINIBOX_STATE_STARTING,     /* Fork/exec in progress */
    MINIBOX_STATE_RUNNING,      /* Successfully executing */
    MINIBOX_STATE_STOPPING,     /* Stop requested, signal dispatched */
    MINIBOX_STATE_COMPLETED,    /* Process completed execution normally */
    MINIBOX_STATE_TERMINATED,   /* Process stopped or killed by signal */
    MINIBOX_STATE_FAILED        /* Launch failed or crashed during setup */
} minibox_state_t;

/**
 * Returns a human-readable string representation of the state.
 */
const char *minibox_state_to_string(minibox_state_t state);

/**
 * Parses a string representation back into a minibox_state_t.
 */
minibox_state_t minibox_state_from_string(const char *str);

/**
 * Checks whether a lifecycle transition from current_state to new_state is valid.
 */
bool minibox_is_valid_transition(minibox_state_t from, minibox_state_t to);

/**
 * Checks whether the state represents a terminal (final) state.
 */
bool minibox_state_is_terminal(minibox_state_t state);

#endif /* MINIBOX_LIFECYCLE_H */
