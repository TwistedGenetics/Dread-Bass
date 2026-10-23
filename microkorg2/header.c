/*
 *  File: header.c
 *
 *  Dread Bass - microKORG 2 oscillator unit header
 *  Names: max 8 characters on the microKORG 2.
 *  Up to 13 parameters, OSC pages 1 (3 knobs), 2 (5) and 3 (5).
 */

#include "unit.h"
#include "runtime.h"

// The load-meter build (tools/mk2-meter) reuses this header with its own
// name and unit ID, so both can be loaded side by side.
#ifndef DREAD_UNIT_NAME
#define DREAD_UNIT_NAME "Dread"
#endif
#ifndef DREAD_UNIT_ID
#define DREAD_UNIT_ID 0x6U  // 1 = VectorFM, 2 = Formant Mutator, 3 = Rollers, 4 = Dusty Pads, 5 = Horizon, 6 = Dread Bass
#endif

const __unit_header unit_header_t unit_header = {
    .header_size = sizeof(unit_header_t),
    .target = UNIT_TARGET_PLATFORM | k_unit_module_osc,
    .api = UNIT_API_VERSION,
    .dev_id = 0x5477476EU,  // 'TwGn' (TwistedGenetics)
    .unit_id = DREAD_UNIT_ID,
    .version = 0x00010200U,  // 1.2.0
    .name = DREAD_UNIT_NAME,
    .num_presets = 0,
    .num_params = 13,
    .params = {
        // min, max, center, default, type, frac, frac mode, reserved, name
        // Order must match dread::Params in core/dreadbass.h

        // Page 1
        {0, 1023, 0, 700, k_unit_param_type_none, 0, 0, 0, {"Timbre"}},
        {0, 100, 0, 0, k_unit_param_type_percent, 0, 0, 0, {"Drive"}},
        {0, 3, 0, 0, k_unit_param_type_strings, 0, 0, 0, {"Mode"}},

        // Page 2
        {0, 100, 0, 55, k_unit_param_type_percent, 0, 0, 0, {"Charactr"}},
        {0, 100, 0, 40, k_unit_param_type_percent, 0, 0, 0, {"Sub"}},
        {0, 1, 0, 0, k_unit_param_type_strings, 0, 0, 0, {"Sub Oct"}},
        {0, 100, 0, 0, k_unit_param_type_percent, 0, 0, 0, {"Knock"}},
        {0, 100, 0, 0, k_unit_param_type_percent, 0, 0, 0, {"Pluck"}},

        // Page 3
        {-24, 48, 0, 0, k_unit_param_type_semi, 0, 0, 0, {"EG Int"}},
        {1, 1000, 1, 300, k_unit_param_type_msec, 0, 0, 0, {"EG Time"}},
        {0, 1, 0, 1, k_unit_param_type_onoff, 0, 0, 0, {"Retrig"}},
        {-24, 24, 0, 0, k_unit_param_type_semi, 0, 0, 0, {"Semi"}},
        {-50, 50, 0, 0, k_unit_param_type_cents, 0, 0, 0, {"Fine"}}}};
