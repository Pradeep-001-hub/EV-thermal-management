/* ============================================================
   FILE: tms_init.c
   EV Thermal Management — Initialization Routines
   ============================================================ */

#include "ev_tms.h"

/* ── Full System Initialization ──────────────────────────── */
void TMS_Init(TMSController_t *tms)
{
    if (tms == NULL) return;

    /* Zero all memory */
    memset(tms, 0, sizeof(TMSController_t));

    /* Set operating mode */
    tms->current_mode = MODE_NORMAL_DRIVE;
    tms->fault_flags  = FAULT_NONE;
    tms->sequence_index = 0;

    /* Initialize sensor defaults */
    TMS_InitSensors(&tms->sensors);

    /* Initialize PID controllers
       TMS_InitPID(pid, Kp,   Ki,    Kd,    setpoint,    min,  max,  dt) */
    TMS_InitPID(&tms->pid_batt,
                 2.5f, 0.05f, 0.8f,
                 28.0f,   /* Battery setpoint: 28°C */
                 0.0f, 255.0f, 0.1f);

    TMS_InitPID(&tms->pid_motor,
                 1.8f, 0.03f, 0.5f,
                 90.0f,   /* Motor setpoint: 90°C */
                 0.0f, 255.0f, 0.1f);

    TMS_InitPID(&tms->pid_cabin,
                 3.0f, 0.08f, 1.0f,
                 22.0f,   /* Cabin setpoint: 22°C */
                 0.0f, 255.0f, 0.1f);

    /* Initialize LSTM network weights to small random values */
    /* In real deployment: load from flash memory            */
    srand(42);
    LSTMNetwork_t *net = &tms->lstm_net;

    for (int i = 0; i < LSTM_HIDDEN_SIZE; i++) {
        for (int j = 0; j < NUM_FEATURES; j++) {
            float r = ((float)rand() / RAND_MAX) * 0.2f - 0.1f;
            net->Wi[i][j] = r;
            net->Wf[i][j] = r * 0.9f;
            net->Wo[i][j] = r * 1.1f;
            net->Wg[i][j] = r * 0.8f;
        }
        for (int j = 0; j < LSTM_HIDDEN_SIZE; j++) {
            float r = ((float)rand() / RAND_MAX) * 0.1f - 0.05f;
            net->Ui[i][j] = r;
            net->Uf[i][j] = r;
            net->Uo[i][j] = r;
            net->Ug[i][j] = r;
        }
        net->bi[i] = 0.0f;
        net->bf[i] = 1.0f;  /* Forget gate bias initialized to 1 */
        net->bo[i] = 0.0f;
        net->bg[i] = 0.0f;
    }

    for (int i = 0; i < NUM_TARGETS; i++) {
        for (int j = 0; j < LSTM_HIDDEN_SIZE; j++) {
            net->Wy[i][j] = ((float)rand() / RAND_MAX) * 0.2f - 0.1f;
        }
        net->by[i] = 0.0f;
    }

    /* Reset LSTM states */
    LSTM_ResetState(net);

    /* Default actuator commands — all safe/off */
    memset(&tms->actuator_cmd, 0, sizeof(ActuatorCmd_t));
    tms->actuator_cmd.heat_pump_enable  = false;
    tms->actuator_cmd.chiller_enable    = false;
    tms->actuator_cmd.immersion_trigger = false;

    printf("[TMS] System initialized. Version %d.%d.%d\n",
           TMS_VERSION_MAJOR,
           TMS_VERSION_MINOR,
           TMS_VERSION_PATCH);
}

/* ── PID Initialization ──────────────────────────────────── */
void TMS_InitPID(PIDController_t *pid,
                 float Kp, float Ki, float Kd,
                 float setpoint,
                 float out_min, float out_max,
                 float dt)
{
    if (pid == NULL) return;
    pid->Kp         = Kp;
    pid->Ki         = Ki;
    pid->Kd         = Kd;
    pid->setpoint   = setpoint;
    pid->prev_error = 0.0f;
    pid->integral   = 0.0f;
    pid->output_min = out_min;
    pid->output_max = out_max;
    pid->dt         = dt;
}

/* ── Sensor Defaults ─────────────────────────────────────── */
void TMS_InitSensors(SensorData_t *sensors)
{
    if (sensors == NULL) return;
    memset(sensors, 0, sizeof(SensorData_t));

    /* Set default ambient */
    sensors->ambient_temp     = 25.0f;
    sensors->ambient_humidity = 50.0f;
    sensors->solar_load       = 0.0f;
    sensors->soc              = 1.0f;  /* Start fully charged */

    /* Initialize all temperature sensors to ambient */
    for (int i = 0; i < NUM_TEMP_SENSORS; i++) {
        sensors->temperature[i] = 25.0f;
    }

    /* Initialize flow sensors to nominal */
    for (int i = 0; i < NUM_FLOW_SENSORS; i++) {
        sensors->flow_rate[i] = 8.0f;  /* 8 L/min nominal */
    }
}

/* ── LSTM State Reset ─────────────────────────────────────── */
void LSTM_ResetState(LSTMNetwork_t *net)
{
    if (net == NULL) return;
    memset(net->h, 0, sizeof(net->h));
    memset(net->c, 0, sizeof(net->c));
}