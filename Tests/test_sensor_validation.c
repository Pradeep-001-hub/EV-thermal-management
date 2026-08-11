/* ============================================================
   FILE: test_sensor_validation.c
   Targeted test for the TMS_ValidateSensors per-channel fix.
   Build: gcc -Iinclude tests/test_sensor_validation.c src/tms_init.c \
              src/tms_sensors.c src/tms_lstm.c -o build/test_sensors -lm
   ============================================================ */

#include "../include/ev_tms.h"
#include <assert.h>

int main(void)
{
    TMSController_t tms;
    TMS_Init(&tms);

    /* Seed distinct, valid readings so each channel has its own history */
    for (int i = 0; i < NUM_TEMP_SENSORS; i++)
        tms.sensors.temperature[i] = 20.0f + i;   /* sensor i -> 20+i degC */

    TMS_ValidateSensors(&tms);   /* seeds last_good_temp cache */
    assert(tms.last_good_temp_init == true);
    assert(fabsf(tms.last_good_temp[3] - 23.0f) < 1e-4f);

    /* Corrupt ONLY sensor 3 (would represent the motor channel) */
    float untouched_value = tms.sensors.temperature[0]; /* battery avg, index 0 */
    tms.sensors.temperature[3] = 999.0f;  /* out of [-40, 200] range */

    TMS_ValidateSensors(&tms);

    /* Bug check: sensor 3 must be restored to ITS OWN last good value
       (23.0), not silently replaced with the battery reading. */
    assert(fabsf(tms.sensors.temperature[3] - 23.0f) < 1e-4f);
    assert(tms.sensors.temperature[3] != untouched_value);
    assert(tms.fault_flags & FAULT_SENSOR_FAILURE);

    /* Sensor 0 (battery) must be unaffected by sensor 3's fault */
    assert(fabsf(tms.sensors.temperature[0] - untouched_value) < 1e-4f);

    printf("ALL TESTS PASSED\n");
    return 0;
}
