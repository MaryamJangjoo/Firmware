#include "inputs.hpp"

// Boolean view of the debounced input states, kept in sync with
// inputs_state[].stableLevel so that existing code that reads
// inputs_value[] keeps working without any changes.
bool inputs_value[INPUTS_NUMBER];

InputConfig inputs_config[INPUTS_NUMBER];
InputState  inputs_state[INPUTS_NUMBER];

// Process-image mirrors used by the registry layer.
uint8_t  inputs_mode_value[INPUTS_NUMBER];
uint8_t  inputs_enabled_value[INPUTS_NUMBER];
uint16_t inputs_event_count_value[INPUTS_NUMBER];
