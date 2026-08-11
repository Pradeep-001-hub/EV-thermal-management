/* ============================================================
   FILE: tms_main.c
   EV Thermal Management — Fault Handling + Main Control Loop
   ============================================================ */

#include "../include/ev_tms.h"

/* ── Fault Checker ───────────────────────────────────────── */
void TMS_CheckFaults(TMSController_t *tms)
{
    ThermalState_t *ts = &tms->thermal_state;
    SensorData_t   *s  = &tms->sensors;

    /* Battery overtemperature */
    if (ts->T_batt_max >= BATT_TEMP_CRITICAL)
        tms->fault_flags |= FAULT_BATT_OVERHEAT;

    /* Motor overtemperature */
    if (ts->T_motor >= MOTOR_TEMP_CRITICAL)
        tms->fault_flags |= FAULT_MOTOR_OVERHEAT;

    /* Inverter overtemperature */
    if (ts->T_inverter >= INV_TEMP_CRITICAL)
        tms->fault_flags |= FAULT_INV_OVERHEAT;

    /* Thermal runaway */
    if (ts->T_batt_max >= BATT_TEMP_RUNAWAY)
        tms->fault_flags |= FAULT_THERMAL_RUNAWAY;

    /* Low flow detection */
    for (int i = 0; i < NUM_FLOW_SENSORS; i++) {
        if (s->flow_rate[i] < 2.0f)
            tms->fault_flags |= FAULT_COOLANT_LOW_FLOW;
    }

    /* Handle faults if any */
    if (tms->fault_flags != FAULT_NONE)
        TMS_HandleFault(tms, tms->fault_flags);
}

/* ── Fault Handler ───────────────────────────────────────── */
void TMS_HandleFault(TMSController_t *tms, FaultCode_t fault)
{
    if (fault & FAULT_THERMAL_RUNAWAY) {
        printf("[FAULT 0x%02X] THERMAL RUNAWAY — Emergency stop!\n",
               fault);
        tms->current_mode = MODE_THERMAL_RUNAWAY;
        TMS_ExecuteMode(tms);
        return;
    }

    if (fault & FAULT_BATT_OVERHEAT) {
        printf("[FAULT 0x%02X] Battery overheat — Max cooling!\n",
               fault);
        tms->actuator_cmd.pump_speed[0]    = 255;
        tms->actuator_cmd.chiller_enable   = true;
        tms->actuator_cmd.compressor_speed = 255;
        tms->actuator_cmd.fan_speed        = 255;
    }

    if (fault & FAULT_MOTOR_OVERHEAT) {
        printf("[FAULT 0x%02X] Motor overheat!\n", fault);
        tms->actuator_cmd.pump_speed[1] = 255;
        tms->actuator_cmd.fan_speed     = 255;
    }

    if (fault & FAULT_COOLANT_LOW_FLOW) {
        printf("[FAULT 0x%02X] Low coolant flow!\n", fault);
        tms->actuator_cmd.pump_speed[0] = 255;
        tms->actuator_cmd.pump_speed[1] = 255;
    }

    if (fault & FAULT_SENSOR_FAILURE) {
        printf("[FAULT 0x%02X] Sensor failure — using estimates\n",
               fault);
    }
}

/* ── Safe Shutdown ───────────────────────────────────────── */
void TMS_SafeShutdown(TMSController_t *tms)
{
    printf("[TMS] Safe shutdown initiated.\n");
    tms->actuator_cmd.heat_pump_enable  = false;
    tms->actuator_cmd.chiller_enable    = false;
    tms->actuator_cmd.compressor_speed  = 0;
    /* Keep pumps and fans running for emergency cooling */
    tms->actuator_cmd.pump_speed[0]     = 255;
    tms->actuator_cmd.pump_speed[1]     = 255;
    tms->actuator_cmd.fan_speed         = 255;
}

/* ── Print Diagnostics ───────────────────────────────────── */
void TMS_PrintDiagnostics(const TMSController_t *tms)
{
    const ThermalState_t *ts = &tms->thermal_state;
    const ActuatorCmd_t  *cmd= &tms->actuator_cmd;

    printf("\n══════════════════════════════════════════\n");
    printf("  EV TMS DIAGNOSTICS  |  t = %u ms\n",
           tms->uptime_ms);
    printf("══════════════════════════════════════════\n");
    printf("  MODE      : %d\n",   tms->current_mode);
    printf("  FAULTS    : 0x%02X\n", tms->fault_flags);
    printf("──────────────────────────────────────────\n");
    printf("  T_batt_avg: %6.2f °C  (max: %6.2f °C)\n",
           ts->T_batt_avg, ts->T_batt_max);
    printf("  T_motor   : %6.2f °C\n", ts->T_motor);
    printf("  T_inverter: %6.2f °C\n", ts->T_inverter);
    printf("  T_cabin   : %6.2f °C\n", ts->T_cabin);
    printf("  T_coolant : in=%.1f°C  out=%.1f°C\n",
           ts->T_coolant_in, ts->T_coolant_out);
    printf("──────────────────────────────────────────\n");
    printf("  Q_batt    : %6.2f W\n", ts->Q_batt_gen);
    printf("  Q_motor   : %6.2f W\n", ts->Q_motor_gen);
    printf("  Q_inv     : %6.2f W\n", ts->Q_inv_gen);
    printf("──────────────────────────────────────────\n");
    printf("  PREDICTED (60s): batt=%.1f motor=%.1f "
           "inv=%.1f cabin=%.1f\n",
           tms->T_batt_pred, tms->T_motor_pred,
           tms->T_inv_pred,  tms->T_cabin_pred);
    printf("──────────────────────────────────────────\n");
    printf("  Pump0=%3d  Pump1=%3d  Fan=%3d  Comp=%3d\n",
           cmd->pump_speed[0], cmd->pump_speed[1],
           cmd->fan_speed, cmd->compressor_speed);
    printf("  HeatPump=%d  Chiller=%d  Immersion=%d\n",
           cmd->heat_pump_enable,
           cmd->chiller_enable,
           cmd->immersion_trigger);
    printf("══════════════════════════════════════════\n\n");
}

/* ── Log State to CSV (telemetry) ────────────────────────────
 * Appends one row per call to TMS_LOG_FILE, creating it with a header
 * row the first time it's written. Intended for post-run analysis of
 * a simulation, or for black-box-style logging on real hardware
 * (subject to flash wear considerations on an actual MCU deployment).
 */
void TMS_LogState(const TMSController_t *tms)
{
    const ThermalState_t *ts = &tms->thermal_state;

    /* Portable existence check (avoids unistd.h/access(), which isn't
       available on all embedded toolchains): try opening for read. */
    bool file_exists = false;
    FILE *probe = fopen(TMS_LOG_FILE, "r");
    if (probe != NULL) {
        file_exists = true;
        fclose(probe);
    }

    FILE *fp = fopen(TMS_LOG_FILE, "a");
    if (fp == NULL) {
        printf("[WARN] Could not open '%s' for logging\n", TMS_LOG_FILE);
        return;
    }

    if (!file_exists) {
        fprintf(fp,
            "uptime_ms,mode,faults,T_batt_avg,T_batt_max,T_motor,"
            "T_inverter,T_cabin,Q_batt_gen,Q_motor_gen,Q_inv_gen,"
            "T_batt_pred,T_motor_pred,T_inv_pred,T_cabin_pred\n");
    }

    fprintf(fp, "%u,%d,0x%02X,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f\n",
            tms->uptime_ms, tms->current_mode, tms->fault_flags,
            ts->T_batt_avg, ts->T_batt_max, ts->T_motor, ts->T_inverter,
            ts->T_cabin, ts->Q_batt_gen, ts->Q_motor_gen, ts->Q_inv_gen,
            tms->T_batt_pred, tms->T_motor_pred, tms->T_inv_pred,
            tms->T_cabin_pred);

    fclose(fp);
}