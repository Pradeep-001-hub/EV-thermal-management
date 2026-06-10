/* ============================================================
   FILE: ev_tms.h
   EV Integrated Thermal Management System
   Header — All structures, constants, enumerations
   Compiler: GCC / ARM-GCC (Cortex-M compatible)
   ============================================================ */

#ifndef EV_TMS_H
#define EV_TMS_H

#include <stdint.h>
#include <stdbool.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── Version ─────────────────────────────────────────────── */
#define TMS_VERSION_MAJOR    1
#define TMS_VERSION_MINOR    0
#define TMS_VERSION_PATCH    0

/* ── System Constants ────────────────────────────────────── */
#define NUM_TEMP_SENSORS     28
#define NUM_PRESSURE_SENSORS  6
#define NUM_FLOW_SENSORS      4
#define NUM_VALVES            8
#define NUM_PUMPS             2
#define SEQ_LENGTH           60     /* LSTM lookback window (s) */
#define NUM_FEATURES         15     /* Input features per timestep */
#define NUM_TARGETS           4     /* Predicted temperatures */
#define LSTM_HIDDEN_SIZE     64     /* LSTM hidden units */
#define PRED_HORIZON         60     /* Prediction horizon (s) */
#define CONTROL_PERIOD_MS   100     /* Control loop period (ms) */
#define CAN_BUS_BAUDRATE    500000  /* 500 kbps */

/* ── Temperature Limits (°C) ─────────────────────────────── */
#define BATT_TEMP_MIN        15.0f
#define BATT_TEMP_MAX        40.0f
#define BATT_TEMP_CRITICAL   55.0f
#define BATT_TEMP_RUNAWAY    80.0f
#define MOTOR_TEMP_MAX      120.0f
#define MOTOR_TEMP_CRITICAL 135.0f
#define INV_TEMP_MAX        150.0f
#define INV_TEMP_CRITICAL   165.0f
#define CABIN_TEMP_MIN       18.0f
#define CABIN_TEMP_MAX       26.0f
#define COOLANT_TEMP_MAX     70.0f

/* ── Valve States ────────────────────────────────────────── */
#define VALVE_CLOSED          0
#define VALVE_OPEN            1
#define VALVE_PARTIAL        -1   /* PWM duty controlled */

/* ── Operating Modes ─────────────────────────────────────── */
typedef enum {
    MODE_NORMAL_DRIVE    = 0,
    MODE_FAST_CHARGE     = 1,
    MODE_COLD_START      = 2,
    MODE_HOT_CLIMATE     = 3,
    MODE_CABIN_HEAT_ONLY = 4,
    MODE_REGEN_CAPTURE   = 5,
    MODE_THERMAL_RUNAWAY = 6,
    MODE_PREDICTIVE_COOL = 7,
    MODE_UNKNOWN         = 8
} OperatingMode_t;

/* ── Fault Codes ─────────────────────────────────────────── */
typedef enum {
    FAULT_NONE              = 0x00,
    FAULT_BATT_OVERHEAT     = 0x01,
    FAULT_MOTOR_OVERHEAT    = 0x02,
    FAULT_INV_OVERHEAT      = 0x04,
    FAULT_COOLANT_LOW_FLOW  = 0x08,
    FAULT_SENSOR_FAILURE    = 0x10,
    FAULT_VALVE_STUCK       = 0x20,
    FAULT_PUMP_FAILURE      = 0x40,
    FAULT_THERMAL_RUNAWAY   = 0x80
} FaultCode_t;

/* ── Sensor Data Structure ───────────────────────────────── */
typedef struct {
    float temperature[NUM_TEMP_SENSORS];  /* °C */
    float pressure[NUM_PRESSURE_SENSORS]; /* bar */
    float flow_rate[NUM_FLOW_SENSORS];    /* L/min */
    float ambient_temp;                   /* °C */
    float ambient_humidity;               /* % RH */
    float solar_load;                     /* W/m² */
    float vehicle_speed;                  /* km/h */
    float battery_current;               /* A */
    float battery_voltage;               /* V */
    float soc;                           /* 0.0 - 1.0 */
    float motor_power;                   /* kW */
    float charge_power;                  /* kW */
    float gps_elevation;                 /* m */
    float gps_grade;                     /* % */
    float temp_forecast;                 /* °C (weather API) */
    uint32_t timestamp_ms;              /* ms since boot */
} SensorData_t;

/* ── Thermal State Structure ─────────────────────────────── */
typedef struct {
    float T_batt_avg;      /* Battery average temperature (°C) */
    float T_batt_max;      /* Battery max cell temperature (°C) */
    float T_batt_min;      /* Battery min cell temperature (°C) */
    float T_batt_delta;    /* Max temperature non-uniformity (°C) */
    float T_motor;         /* Motor winding temperature (°C) */
    float T_inverter;      /* Inverter junction temperature (°C) */
    float T_cabin;         /* Cabin air temperature (°C) */
    float T_coolant_in;    /* Coolant inlet temperature (°C) */
    float T_coolant_out;   /* Coolant outlet temperature (°C) */
    float Q_batt_gen;      /* Battery heat generation (W) */
    float Q_motor_gen;     /* Motor heat generation (W) */
    float Q_inv_gen;       /* Inverter heat generation (W) */
} ThermalState_t;

/* ── Actuator Command Structure ──────────────────────────── */
typedef struct {
    uint8_t  valve_state[NUM_VALVES];  /* 0=closed, 1=open */
    uint8_t  valve_pwm[NUM_VALVES];    /* 0-255 PWM duty */
    uint8_t  pump_speed[NUM_PUMPS];    /* 0-255 PWM duty */
    uint8_t  compressor_speed;         /* 0-255 PWM duty */
    uint8_t  fan_speed;                /* 0-255 PWM duty */
    uint8_t  peltier_duty;             /* 0-255 PWM duty */
    bool     immersion_trigger;        /* Emergency immersion */
    bool     heat_pump_enable;         /* Heat pump on/off */
    bool     chiller_enable;           /* Chiller on/off */
} ActuatorCmd_t;

/* ── LSTM Network Structure ──────────────────────────────── */
typedef struct {
    /* Input gate weights */
    float Wi[LSTM_HIDDEN_SIZE][NUM_FEATURES];
    float Ui[LSTM_HIDDEN_SIZE][LSTM_HIDDEN_SIZE];
    float bi[LSTM_HIDDEN_SIZE];

    /* Forget gate weights */
    float Wf[LSTM_HIDDEN_SIZE][NUM_FEATURES];
    float Uf[LSTM_HIDDEN_SIZE][LSTM_HIDDEN_SIZE];
    float bf[LSTM_HIDDEN_SIZE];

    /* Output gate weights */
    float Wo[LSTM_HIDDEN_SIZE][NUM_FEATURES];
    float Uo[LSTM_HIDDEN_SIZE][LSTM_HIDDEN_SIZE];
    float bo[LSTM_HIDDEN_SIZE];

    /* Cell gate weights */
    float Wg[LSTM_HIDDEN_SIZE][NUM_FEATURES];
    float Ug[LSTM_HIDDEN_SIZE][LSTM_HIDDEN_SIZE];
    float bg[LSTM_HIDDEN_SIZE];

    /* Output layer weights */
    float Wy[NUM_TARGETS][LSTM_HIDDEN_SIZE];
    float by[NUM_TARGETS];

    /* Hidden and cell states */
    float h[LSTM_HIDDEN_SIZE];   /* Hidden state */
    float c[LSTM_HIDDEN_SIZE];   /* Cell state */
} LSTMNetwork_t;

/* ── PID Controller Structure ────────────────────────────── */
typedef struct {
    float Kp;           /* Proportional gain */
    float Ki;           /* Integral gain */
    float Kd;           /* Derivative gain */
    float setpoint;     /* Target value */
    float prev_error;   /* Previous error */
    float integral;     /* Integral accumulator */
    float output_min;   /* Output clamp minimum */
    float output_max;   /* Output clamp maximum */
    float dt;           /* Time step (s) */
} PIDController_t;

/* ── Main TMS Controller Structure ──────────────────────────*/
typedef struct {
    SensorData_t    sensors;
    ThermalState_t  thermal_state;
    ActuatorCmd_t   actuator_cmd;
    LSTMNetwork_t   lstm_net;
    PIDController_t pid_batt;
    PIDController_t pid_motor;
    PIDController_t pid_cabin;
    OperatingMode_t current_mode;
    FaultCode_t     fault_flags;

    /* LSTM sequence buffer */
    float sequence_buffer[SEQ_LENGTH][NUM_FEATURES];
    int   sequence_index;

    /* Predicted temperatures */
    float T_batt_pred;
    float T_motor_pred;
    float T_inv_pred;
    float T_cabin_pred;

    /* Energy tracking */
    float cooling_energy_J;   /* Total cooling energy (J) */
    float heating_energy_J;   /* Total heating energy (J) */
    uint32_t uptime_ms;

} TMSController_t;

/* ── Function Prototypes ─────────────────────────────────── */

/* Section 2: Initialization */
void TMS_Init(TMSController_t *tms);
void TMS_InitPID(PIDController_t *pid, float Kp, float Ki,
                 float Kd, float sp, float min, float max, float dt);
void TMS_LoadLSTMWeights(LSTMNetwork_t *net, const char *filepath);
void TMS_InitSensors(SensorData_t *sensors);

/* Section 3: Sensor Processing */
void TMS_ReadSensors(TMSController_t *tms);
void TMS_UpdateThermalState(TMSController_t *tms);
void TMS_ValidateSensors(TMSController_t *tms);
float TMS_CalcHeatGeneration(float current, float resistance);

/* Section 4: LSTM Predictor */
void   LSTM_Forward(LSTMNetwork_t *net,
                    float input[NUM_FEATURES],
                    float output[NUM_TARGETS]);
float  LSTM_Sigmoid(float x);
float  LSTM_Tanh(float x);
void   LSTM_ResetState(LSTMNetwork_t *net);
void   TMS_UpdateSequenceBuffer(TMSController_t *tms);
void   TMS_RunLSTMPrediction(TMSController_t *tms);

/* Section 5: Mode Decision & Control */
OperatingMode_t TMS_DecideMode(TMSController_t *tms);
void TMS_ExecuteMode(TMSController_t *tms);
float TMS_PIDUpdate(PIDController_t *pid, float measured);
void TMS_SetValves(TMSController_t *tms,
                   uint8_t v0, uint8_t v1, uint8_t v2,
                   uint8_t v3, uint8_t v4, uint8_t v5,
                   uint8_t v6, uint8_t v7);
void TMS_SetPumps(TMSController_t *tms,
                  uint8_t pump0_pwm, uint8_t pump1_pwm);

/* Section 6: Fault Handling & Diagnostics */
void TMS_CheckFaults(TMSController_t *tms);
void TMS_HandleFault(TMSController_t *tms, FaultCode_t fault);
void TMS_SafeShutdown(TMSController_t *tms);
void TMS_LogState(const TMSController_t *tms);
void TMS_PrintDiagnostics(const TMSController_t *tms);

#endif /* EV_TMS_H */
