#include "inputs.hpp"

// Boolean view of the debounced input states, kept in sync with
// inputs_state[].stableLevel so that existing code that reads
// inputs_value[] keeps working without any changes.
bool inputs_value[INPUTS_NUMBER];

InputConfig inputs_config[INPUTS_NUMBER];
InputState  inputs_state[INPUTS_NUMBER];

// Internal process-image mirrors used only by the controller.
uint8_t  inputs_internal_mode[INPUTS_NUMBER];
uint8_t  inputs_internal_enabled[INPUTS_NUMBER];
uint16_t inputs_internal_event_count[INPUTS_NUMBER];