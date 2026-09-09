#include "ecosmart_registeries.h"
#include "inputs.hpp"
#include "Outputs.hpp"

reg_module_input_t reg_module_input;
reg_module_output_t reg_module_output;

void ecosmart_registery_init()
{
    // Initialize inputs registery
    for (size_t i = 0; i < INPUTS_NUMBER; i++)
    {
        reg_module_input.state[i].address = REG_ADD_INPUT_STATE[i];
        reg_module_input.state[i].datatype = reg_datatype_bit;
        reg_module_input.state[i].size = sizeof(inputs_value[0]);
        reg_module_input.state[i].ref = &inputs_value[i];
    }
    // Initialize outputs registery
    for (size_t i = 0; i < OUTPUTS_NUMBER; i++)
    {
        reg_module_output.state[i].address = REG_ADD_OUTPUT_STATE[i];
        reg_module_output.state[i].datatype = reg_datatype_bit;
        reg_module_output.state[i].size = sizeof(outputs_object[0].value);
        reg_module_output.state[i].ref = &outputs_object[i].value;

        reg_module_output.timer_permanent[i].address = REG_ADD_OUTPUT_TIMER_PERMANENT[i];
        reg_module_output.timer_permanent[i].datatype = reg_datatype_uint16;
        reg_module_output.timer_permanent[i].size = sizeof(outputs_object[0].timer_permanent);
        reg_module_output.timer_permanent[i].ref = &outputs_object[i].timer_permanent;
        

        reg_module_output.timer_sleep[i].address = REG_ADD_OUTPUT_TIMER_SLEEP[i];
        reg_module_output.timer_sleep[i].datatype = reg_datatype_uint16;
        reg_module_output.timer_sleep[i].size = sizeof(outputs_object[0].timer_sleep);
        reg_module_output.timer_sleep[i].ref = &outputs_object[i].timer_sleep;
    }
}