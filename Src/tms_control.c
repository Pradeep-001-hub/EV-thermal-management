/* ============================================================
   FILE: tms_control.c
   EV Thermal Management — Mode Decision & Actuator Control
   ============================================================ */

#include "../include/ev_tms.h"

/* ── PID Update ──────────────────────────────────────────── */
float TMS_PIDUpdate(PIDController_t *pid, float measured)
{
    float error      = pid->setpoint - measured;
    pid->integral   += error * pid->dt;

    /* Anti-windup: clamp integral */
    float max_int = (pid->output_max - pid->output_min) / pid->Ki;
    if (pid->Ki > 0.0f) {
        if (pid->integral >  max_int) pid->integral =  max_int;
        if (pid->integral < -max_int) pid->integral = -max_int;
    }

    float derivative = (error - pid->prev_error) / pid->dt;
    pid->prev_error  = error;

    float output = pid->Kp * error
                 + pid->Ki * pid->integral
                 + pid->Kd * derivative;

    /* Clamp output */
    if (output > pid->output_max) output = pid->output_max;
    if (output < pid->output_min) output = pid->output_min;

    return output;
}

/* ── Set Valve States ────────────────────────────────────── */
void TMS_SetValves(TMSController_t *tms,
                   uint8_t v0, uint8_t v1, uint8_t v2, uint8_t v3,
                   uint8_t v4, uint8_t v5, uint8_t v6, uint8_t v7)
{
    tms->actuator_cmd.valve_state[0] = v0;
    tms->actuator_cmd.valve_state[1] = v1;
    tms->actuator_cmd.valve_state[2] = v2;
    tms->actuator_cmd.valve_state[3] = v3;
    tms->actuator_cmd.valve_state[4] = v4;
    tms->actuator_cmd.valve_state[5] = v5;
    tms->actuator_cmd.valve_state[6] = v6;
    tms->actuator_cmd.valve_state[7] = v7;
}

/* ── Set Pump Speeds ─────────────────────────────────────── */
void TMS_SetPumps(TMSController_t *tms,
                  uint8_t pump0_pwm, uint8_t pump1_pwm)
{
    tms->actuator_cmd.pump_speed[0] = pump0_pwm;
    tms->actuator_cmd.pump_speed[1] = pump1_pwm;
}

/* ── Mode Decision Engine ────────────────────────────────── */
OperatingMode_t TMS_DecideMode(TMSController_t *tms)
{
    ThermalState_t *ts = &tms->thermal_state;
    SensorData_t   *s  = &tms->sensors;

    /* Priority 1: Safety — Thermal runaway always first */
    if (ts->T_batt_max >= BATT_TEMP_RUNAWAY)
        return MODE_THERMAL_RUNAWAY;

    /* Priority 2: Fast charging active */
    if (s->charge_power > 50.0f)
        return MODE_FAST_CHARGE;

    /* Priority 3: Cold start */
    if (ts->T_batt_avg < BATT_TEMP_MIN && s->vehicle_speed < 5.0f)
        return MODE_COLD_START;

    /* Priority 4: AI predicts overheating in 60 seconds */
    if (tms->sequence_index >= SEQ_LENGTH) {
        if (tms->T_batt_pred  > (BATT_TEMP_MAX  - 3.0f) ||
            tms->T_motor_pred > (MOTOR_TEMP_MAX  - 5.0f) ||
            tms->T_inv_pred   > (INV_TEMP_MAX    - 5.0f))
            return MODE_PREDICTIVE_COOL;
    }

    /* Priority 5: Hot ambient climate */
    if (s->ambient_temp > 35.0f)
        return MODE_HOT_CLIMATE;

    /* Priority 6: Regenerative braking (speed drop + motor cool) */
    if (s->vehicle_speed > 60.0f && ts->T_motor > 100.0f)
        return MODE_REGEN_CAPTURE;

    /* Priority 7: Parked cold, cabin heat needed */
    if (s->vehicle_speed < 1.0f && ts->T_cabin < CABIN_TEMP_MIN)
        return MODE_CABIN_HEAT_ONLY;

    /* Default: Normal drive */
    return MODE_NORMAL_DRIVE;
}

/* ── Execute Control Actions per Mode ────────────────────── */
void TMS_ExecuteMode(TMSController_t *tms)
{
    ThermalState_t *ts  = &tms->thermal_state;
    ActuatorCmd_t  *cmd = &tms->actuator_cmd;

    float pump_pwm, fan_pwm, comp_pwm;

    switch (tms->current_mode) {

    /* ── Mode 0: Normal Drive ──────────────────────────── */
    case MODE_NORMAL_DRIVE:
        /* Battery PID → pump 0 speed */
        pump_pwm = TMS_PIDUpdate(&tms->pid_batt, ts->T_batt_avg);
        TMS_SetPumps(tms, (uint8_t)pump_pwm, 80);
        TMS_SetValves(tms, 1,1,0,0, 0,0,0,0);
        cmd->heat_pump_enable = false;
        cmd->chiller_enable   = false;
        cmd->fan_speed        = (uint8_t)(pump_pwm * 0.6f);
        break;

    /* ── Mode 1: Fast Charge ───────────────────────────── */
    case MODE_FAST_CHARGE:
        /* Maximum battery cooling — all resources to BTMS */
        TMS_SetPumps(tms, 255, 200);
        TMS_SetValves(tms, 1,1,1,1, 0,0,0,0);
        cmd->chiller_enable  = true;
        cmd->heat_pump_enable= false;
        cmd->compressor_speed= 200;
        cmd->fan_speed       = 255;
        printf("[MODE] Fast Charge: Max cooling active\n");
        break;

    /* ── Mode 2: Cold Start ────────────────────────────── */
    case MODE_COLD_START:
        /* Route motor/inverter waste heat to battery */
        TMS_SetValves(tms, 0,1,0,1, 1,0,0,0);
        TMS_SetPumps(tms, 60, 100);
        cmd->heat_pump_enable = true;
        cmd->chiller_enable   = false;
        cmd->compressor_speed = 100;
        printf("[MODE] Cold Start: Waste heat to battery\n");
        break;

    /* ── Mode 3: Hot Climate ───────────────────────────── */
    case MODE_HOT_CLIMATE:
        /* All circuits active — max dissipation */
        pump_pwm = TMS_PIDUpdate(&tms->pid_batt, ts->T_batt_avg);
        fan_pwm  = TMS_PIDUpdate(&tms->pid_motor, ts->T_motor);
        comp_pwm = TMS_PIDUpdate(&tms->pid_cabin, ts->T_cabin);
        TMS_SetPumps(tms, (uint8_t)pump_pwm, 255);
        TMS_SetValves(tms, 1,1,1,1, 1,1,0,0);
        cmd->heat_pump_enable = true;
        cmd->chiller_enable   = true;
        cmd->fan_speed        = (uint8_t)fan_pwm;
        cmd->compressor_speed = (uint8_t)comp_pwm;
        break;

    /* ── Mode 4: Cabin Heat Only ───────────────────────── */
    case MODE_CABIN_HEAT_ONLY:
        TMS_SetValves(tms, 0,0,0,0, 1,1,0,0);
        TMS_SetPumps(tms, 0, 80);
        cmd->heat_pump_enable = true;
        cmd->chiller_enable   = false;
        comp_pwm = TMS_PIDUpdate(&tms->pid_cabin, ts->T_cabin);
        cmd->compressor_speed = (uint8_t)comp_pwm;
        break;

    /* ── Mode 5: Regenerative Capture ─────────────────── */
    case MODE_REGEN_CAPTURE:
        /* Route motor waste heat to heat pump */
        TMS_SetValves(tms, 1,0,0,1, 1,0,1,0);
        TMS_SetPumps(tms, 150, 120);
        cmd->heat_pump_enable = true;
        cmd->chiller_enable   = false;
        cmd->compressor_speed = 80;
        break;

    /* ── Mode 6: Thermal Runaway ───────────────────────── */
    case MODE_THERMAL_RUNAWAY:
        TMS_SafeShutdown(tms);
        cmd->immersion_trigger = true;
        TMS_SetPumps(tms, 255, 255);
        TMS_SetValves(tms, 1,1,1,1, 1,1,1,1);
        cmd->fan_speed = 255;
        printf("[CRITICAL] THERMAL RUNAWAY — Immersion triggered!\n");
        break;

    /* ── Mode 7: Predictive Pre-cool ──────────────────── */
    case MODE_PREDICTIVE_COOL:
        /* Pre-cool based on LSTM prediction */
        pump_pwm = 180;  /* Elevated cooling before heat arrives */
        TMS_SetPumps(tms, (uint8_t)pump_pwm, 160);
        TMS_SetValves(tms, 1,1,1,0, 0,0,0,0);
        cmd->chiller_enable   = true;
        cmd->heat_pump_enable = false;
        cmd->compressor_speed = 150;
        cmd->fan_speed        = 200;
        printf("[AI] Predictive pre-cool: T_batt_pred=%.1f°C\n",
               tms->T_batt_pred);
        break;

    default:
        break;
    }
}