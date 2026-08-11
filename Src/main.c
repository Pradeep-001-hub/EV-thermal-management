/* ============================================================
   FILE: main.c
   EV Thermal Management — Main Control Loop
   ============================================================ */

#include "../include/ev_tms.h"

int main(void)
{
    TMSController_t tms;

    /* Initialize full system */
    TMS_Init(&tms);

    printf("[TMS] Starting control loop...\n\n");

    /* Simulate 1000 control cycles (100s at 100ms period) */
    for (uint32_t cycle = 0; cycle < 1000; cycle++) {

        tms.uptime_ms = cycle * CONTROL_PERIOD_MS;

        /* ── Step 1: Read all sensors ─────────────────── */
        TMS_ReadSensors(&tms);

        /* ── Step 2: Validate sensor data ────────────── */
        TMS_ValidateSensors(&tms);

        /* ── Step 3: Update thermal state ────────────── */
        TMS_UpdateThermalState(&tms);

        /* ── Step 4: Update LSTM sequence buffer ──────── */
        TMS_UpdateSequenceBuffer(&tms);

        /* ── Step 5: Run LSTM prediction ─────────────── */
        TMS_RunLSTMPrediction(&tms);

        /* ── Step 6: Decide operating mode ───────────── */
        tms.current_mode = TMS_DecideMode(&tms);

        /* ── Step 7: Execute control actions ─────────── */
        TMS_ExecuteMode(&tms);

        /* ── Step 8: Check for faults ─────────────────── */
        TMS_CheckFaults(&tms);

        /* ── Step 9: Log telemetry every cycle, print full
                      diagnostics every 10 cycles (1 second) ── */
        TMS_LogState(&tms);
        if (cycle % 10 == 0)
            TMS_PrintDiagnostics(&tms);
    }

    printf("[TMS] Simulation complete. Telemetry written to %s\n",
           TMS_LOG_FILE);
    return 0;
}
