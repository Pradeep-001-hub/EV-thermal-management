/* ============================================================
   FILE: tms_sensors.c
   EV Thermal Management — Sensor Reading & Processing
   ============================================================ */

#include "../include/ev_tms.h"

/* ── Read All Sensors (HAL abstraction) ──────────────────── */
void TMS_ReadSensors(TMSController_t *tms)
{
    /*
     * In real hardware: replace with ADC/CAN/LIN reads
     * Here: simulates a drive cycle for validation
     */
    static uint32_t tick = 0;
    tick++;

    SensorData_t *s = &tms->sensors;
    s->timestamp_ms = tick * CONTROL_PERIOD_MS;

    float t = (float)tick * 0.1f;  /* Time in seconds */

    /* Simulate drive cycle temperatures */
    s->temperature[0]  = 25.0f + 0.003f * t
                         + 5.0f * sinf(t / 300.0f);   /* T_batt_avg */
    s->temperature[1]  = s->temperature[0] + 3.0f;   /* T_batt_max */
    s->temperature[2]  = s->temperature[0] - 2.0f;   /* T_batt_min */
    s->temperature[3]  = 50.0f + 0.005f * t
                         + 10.0f * sinf(t / 200.0f); /* T_motor */
    s->temperature[4]  = 60.0f + 0.004f * t
                         + 8.0f * sinf(t / 200.0f);  /* T_inverter */
    s->temperature[5]  = 22.0f + 2.0f
                         * sinf(t / 1000.0f);         /* T_cabin */
    s->temperature[6]  = 30.0f + 0.002f * t;         /* T_coolant_in */
    s->temperature[7]  = s->temperature[6] + 8.0f;   /* T_coolant_out */

    /* Fill remaining sensors with plausible values */
    for (int i = 8; i < NUM_TEMP_SENSORS; i++) {
        s->temperature[i] = s->temperature[0]
                            + (float)(i - 8) * 0.5f;
    }

    /* Pressure sensors */
    for (int i = 0; i < NUM_PRESSURE_SENSORS; i++) {
        s->pressure[i] = 2.5f + 0.1f * sinf(t / 100.0f);
    }

    /* Flow sensors */
    for (int i = 0; i < NUM_FLOW_SENSORS; i++) {
        s->flow_rate[i] = 8.0f + sinf(t / 50.0f);
    }

    /* Vehicle state */
    s->vehicle_speed    = 60.0f + 30.0f * sinf(t / 500.0f);
    s->battery_current  = 80.0f + 20.0f * sinf(t / 300.0f);
    s->battery_voltage  = 380.0f - 0.001f * t;
    s->soc              = (1.0f - 0.00002f * t);
    if (s->soc < 0.05f) s->soc = 0.05f;

    s->motor_power      = s->vehicle_speed * 0.25f;
    s->charge_power     = 0.0f;  /* Not charging while driving */
    s->ambient_temp     = 28.0f + 5.0f * sinf(t / 3600.0f);
    s->temp_forecast    = s->ambient_temp + 2.0f;
    s->gps_elevation    = 200.0f + 50.0f * sinf(t / 600.0f);
    s->gps_grade        = 2.0f * sinf(t / 300.0f);
    s->solar_load       = 400.0f * fmaxf(0.0f, sinf(t / 43200.0f));
}

/* ── Update Thermal State from Sensor Readings ───────────── */
void TMS_UpdateThermalState(TMSController_t *tms)
{
    SensorData_t  *s  = &tms->sensors;
    ThermalState_t *ts = &tms->thermal_state;

    ts->T_batt_avg   = s->temperature[0];
    ts->T_batt_max   = s->temperature[1];
    ts->T_batt_min   = s->temperature[2];
    ts->T_batt_delta = ts->T_batt_max - ts->T_batt_min;
    ts->T_motor      = s->temperature[3];
    ts->T_inverter   = s->temperature[4];
    ts->T_cabin      = s->temperature[5];
    ts->T_coolant_in = s->temperature[6];
    ts->T_coolant_out= s->temperature[7];

    /* Estimate heat generation using I²R model */
    float R_cell  = 0.002f;  /* 2 mΩ per cell (typical Li-ion) */
    float R_motor = 0.05f;   /* 50 mΩ motor winding resistance */
    float R_inv   = 0.001f;  /* 1 mΩ inverter equivalent */

    ts->Q_batt_gen  = TMS_CalcHeatGeneration(s->battery_current, R_cell);
    ts->Q_motor_gen = TMS_CalcHeatGeneration(
                          s->motor_power * 1000.0f / 380.0f, R_motor);
    ts->Q_inv_gen   = TMS_CalcHeatGeneration(
                          s->motor_power * 1000.0f / 380.0f, R_inv);
}

/* ── Heat Generation: I²R Model ──────────────────────────── */
float TMS_CalcHeatGeneration(float current, float resistance)
{
    return current * current * resistance;  /* P = I²R (Watts) */
}

/* ── Sensor Validation ───────────────────────────────────── */
void TMS_ValidateSensors(TMSController_t *tms)
{
    SensorData_t *s = &tms->sensors;

    /* On the very first call there is no history yet -- seed the cache
       with whatever the sensors currently report so validation has a
       sane fallback from cycle 1 onward. */
    if (!tms->last_good_temp_init) {
        memcpy(tms->last_good_temp, s->temperature,
               sizeof(float) * NUM_TEMP_SENSORS);
        tms->last_good_temp_init = true;
    }

    /* Check for out-of-range temperature readings */
    for (int i = 0; i < NUM_TEMP_SENSORS; i++) {
        if (s->temperature[i] < -40.0f || s->temperature[i] > 200.0f) {
            tms->fault_flags |= FAULT_SENSOR_FAILURE;
            printf("[WARN] Sensor %d out of range: %.1f°C\n",
                   i, s->temperature[i]);
            /* Replace with THIS sensor's own last known good value --
               not an unrelated channel (e.g. a bad motor sensor must
               not be silently papered over with the battery reading). */
            s->temperature[i] = tms->last_good_temp[i];
        } else {
            /* Valid reading: update the cache for next time */
            tms->last_good_temp[i] = s->temperature[i];
        }
    }

    /* Check flow sensors */
    for (int i = 0; i < NUM_FLOW_SENSORS; i++) {
        if (s->flow_rate[i] < 1.0f) {
            tms->fault_flags |= FAULT_COOLANT_LOW_FLOW;
            printf("[WARN] Low flow on circuit %d: %.2f L/min\n",
                   i, s->flow_rate[i]);
        }
    }

    /* SOC range check */
    if (s->soc < 0.0f || s->soc > 1.0f) {
        s->soc = (s->soc < 0.0f) ? 0.0f : 1.0f;
    }
}