#include "bambu_state.h"
#include <string.h>

GcodeState parseGcodeState(const char* raw) {
    if (!raw || !raw[0])                  return STATE_UNKNOWN;
    if (strcmp(raw, "RUNNING") == 0)       return STATE_RUNNING;
    if (strcmp(raw, "PAUSE")   == 0)       return STATE_PAUSE;
    if (strcmp(raw, "FINISH")  == 0)       return STATE_FINISH;
    if (strcmp(raw, "IDLE")    == 0)       return STATE_IDLE;
    if (strcmp(raw, "FAILED")  == 0)       return STATE_FAILED;
    if (strcmp(raw, "PREPARE") == 0)       return STATE_PREPARE;
    return STATE_UNKNOWN;
}

const char* gcodeStateLabel(GcodeState s) {
    switch (s) {
        case STATE_IDLE:    return "Ready";
        case STATE_RUNNING: return "Running";
        case STATE_PAUSE:   return "Paused";
        case STATE_PREPARE: return "Preparing";
        case STATE_FINISH:  return "Finished";
        case STATE_FAILED:  return "Error";
        default:            return "Waiting...";
    }
}

const char* const DAYS_SHORT[7]   = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
const char* const MONTHS_SHORT[12] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                       "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
