/*
 * ============================================================
 *  EV THERMAL MANAGEMENT — PID Coolant Flow Controller
 *  File   : pid_thermal_controller.c
 *  Purpose: Regulate battery pack temperature via variable
 *           coolant mass-flow rate using discrete PID control
 * ============================================================
 */

#include <stdio.h>
#include <stdlib.h>
#include <math.h>

/* ── Setpoints & Safety ─────────────────────────────────── */
#define T_SETPOINT      308.15    /* K  target temp (35°C)    */
#define T_DEADBAND      1.0       /* K  ±1 K deadband         */
#define FLOW_MIN        0.001     /* kg/s  minimum flow rate  */
#define FLOW_MAX        0.050     /* kg/s  maximum flow rate  */
#define FLOW_INIT       0.005     /* kg/s  initial flow rate  */

/* ── PID Tuning Parameters ──────────────────────────────── */
/* Tuned for battery thermal mass ~4.5 kg, time constant ~180s */
#define KP              0.008     /* proportional gain        */
#define KI              0.00005   /* integral gain            */
#define KD              0.002     /* derivative gain          */
#define DT              1.0       /* s  sample period         */
#define I_MAX           0.020     /* anti-windup integral cap */

/* ── Thermal System (simplified 1st-order) ──────────────── */
#define PACK_MASS       4.5       /* kg total battery mass    */
#define PACK_CP         1050.0    /* J/(kg·K)                 */
#define COOLANT_CP      3800.0    /* J/(kg·K) 50:50 glycol    */
#define COOLANT_T_IN    291.15    /* K  coolant inlet         */

#define SIM_TIME        7200      /* s  2-hour test           */
#define MAX_STEPS       7201

/* ─────────────────────────────────────────────────────────
   PID Controller State
   ───────────────────────────────────────────────────────── */
typedef struct {
    double kp, ki, kd;
    double dt;
    double integral;
    double prev_error;
    double i_max;         /* anti-windup cap      */
    double output_min;
    double output_max;
} PID;

void pid_init(PID *p, double kp, double ki, double kd,
              double dt, double i_max, double out_min, double out_max) {
    p->kp = kp; p->ki = ki; p->kd = kd;
    p->dt = dt; p->i_max = i_max;
    p->integral = 0.0; p->prev_error = 0.0;
    p->output_min = out_min; p->output_max = out_max;
}

double pid_update(PID *p, double setpoint, double measured) {
    double error = setpoint - measured;

    /* Deadband: ignore small deviations */
    if (fabs(error) < T_DEADBAND) error = 0.0;

    /* Integral with anti-windup clamping */
    p->integral += error * p->dt;
    if      (p->integral >  p->i_max) p->integral =  p->i_max;
    else if (p->integral < -p->i_max) p->integral = -p->i_max;

    /* Derivative (backward difference) */
    double derivative = (error - p->prev_error) / p->dt;
    p->prev_error = error;

    double u = p->kp * error + p->ki * p->integral + p->kd * derivative;

    /* Clamp output */
    if (u < p->output_min) u = p->output_min;
    if (u > p->output_max) u = p->output_max;

    return u;
}

/* ─────────────────────────────────────────────────────────
   Thermal plant model  (1st-order ODE, Euler integration)
   dT/dt = (Q_gen - Q_cool) / (m * Cp)
   Q_cool = m_dot * Cp_coolant * (T - T_in)
   ───────────────────────────────────────────────────────── */
double heat_disturbance(double t) {
    /* Simulate varying load: highway + stop-go segments */
    double cycle = fmod(t, 600.0);
    if (cycle < 200)       return 180.0 + 60.0  * sin(2*M_PI*cycle/200.0);
    else if (cycle < 400)  return 80.0  + 20.0  * sin(2*M_PI*cycle/100.0);
    else                   return 250.0 * fabs(sin(M_PI*cycle/200.0));
}

/* ─────────────────────────────────────────────────────────
   MAIN SIMULATION
   ───────────────────────────────────────────────────────── */
int main(void) {
    printf("=== PID Thermal Controller for EV Battery Pack ===\n\n");

    PID ctrl;
    pid_init(&ctrl, KP, KI, KD, DT, I_MAX, FLOW_MIN, FLOW_MAX);

    double T        = 300.15;     /* K  initial pack temp     */
    double mdot     = FLOW_INIT;  /* kg/s coolant flow rate   */
    double thermal_mass = PACK_MASS * PACK_CP;

    /* Output CSV */
    FILE *fp = fopen("pid_controller_results.csv", "w");
    if (!fp) { perror("fopen"); return 1; }
    fprintf(fp, "time_s,T_pack_K,T_pack_C,setpoint_C,error_K,"
                "flow_kg_s,Q_gen_W,Q_cool_W,integral\n");

    double perf_error_sq = 0.0;
    int    n_overshoot   = 0;
    double T_max         = 0.0;

    printf("%-8s %-10s %-10s %-12s %-12s\n",
           "Time(s)", "T(°C)", "SP(°C)", "Flow(kg/s)", "Q_gen(W)");
    printf("%-8s %-10s %-10s %-12s %-12s\n",
           "-------","------","------","---------","--------");

    for (int step = 0; step <= SIM_TIME; step++) {
        double t    = (double)step;
        double Q_gen  = heat_disturbance(t);
        double Q_cool = mdot * COOLANT_CP * (T - COOLANT_T_IN);
        if (Q_cool < 0.0) Q_cool = 0.0;   /* no reverse heat transfer */

        /* Temperature update (Euler) */
        double dT = (Q_gen - Q_cool) / thermal_mass * DT;
        T += dT;

        /* PID output = new flow rate */
        mdot = pid_update(&ctrl, T_SETPOINT, T);

        /* Performance metrics */
        double err = T_SETPOINT - T;
        perf_error_sq += err * err;
        if (T > T_SETPOINT + 2.0) n_overshoot++;
        if (T > T_max) T_max = T;

        /* CSV write */
        fprintf(fp, "%.0f,%.4f,%.4f,%.4f,%.4f,%.6f,%.2f,%.2f,%.6f\n",
                t, T, T-273.15, T_SETPOINT-273.15,
                err, mdot, Q_gen, Q_cool, ctrl.integral);

        /* Console print every 60 s */
        if (step % 60 == 0) {
            printf("%-8.0f %-10.2f %-10.2f %-12.5f %-12.1f\n",
                   t, T-273.15, T_SETPOINT-273.15, mdot, Q_gen);
        }
    }
    fclose(fp);

    double rmse = sqrt(perf_error_sq / (SIM_TIME + 1));
    printf("\n========================================\n");
    printf("  PID CONTROLLER PERFORMANCE SUMMARY\n");
    printf("========================================\n");
    printf("  RMSE (temp error)     : %.4f K\n",    rmse);
    printf("  Max overshoot time    : %d s\n",       n_overshoot);
    printf("  Peak temperature      : %.2f°C\n",    T_max - 273.15);
    printf("  Setpoint              : %.2f°C\n",    T_SETPOINT - 273.15);
    printf("  Results saved         : pid_controller_results.csv\n");
    printf("========================================\n");

    return 0;
}

/*
 * COMPILE:
 *   gcc -O2 -Wall pid_thermal_controller.c -o pid_controller -lm
 * RUN:
 *   ./pid_controller
 */
