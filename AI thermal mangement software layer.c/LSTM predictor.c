/* ============================================================
   FILE: tms_lstm.c
   EV Thermal Management — LSTM Forward Pass
   Real-time temperature prediction engine
   ============================================================ */

#include "ev_tms.h"

/* ── Activation: Sigmoid ─────────────────────────────────── */
float LSTM_Sigmoid(float x)
{
    /* Numerically stable sigmoid */
    if (x >= 0.0f)
        return 1.0f / (1.0f + expf(-x));
    else {
        float ex = expf(x);
        return ex / (1.0f + ex);
    }
}

/* ── Activation: Tanh ────────────────────────────────────── */
float LSTM_Tanh(float x)
{
    return tanhf(x);
}

/* ── LSTM Single Timestep Forward Pass ───────────────────── */
void LSTM_Forward(LSTMNetwork_t *net,
                  float input[NUM_FEATURES],
                  float output[NUM_TARGETS])
{
    float i_gate[LSTM_HIDDEN_SIZE];  /* Input gate */
    float f_gate[LSTM_HIDDEN_SIZE];  /* Forget gate */
    float o_gate[LSTM_HIDDEN_SIZE];  /* Output gate */
    float g_gate[LSTM_HIDDEN_SIZE];  /* Cell gate */
    float c_new[LSTM_HIDDEN_SIZE];   /* New cell state */
    float h_new[LSTM_HIDDEN_SIZE];   /* New hidden state */

    /* ── Compute all four gates ──────────────────────────── */
    for (int j = 0; j < LSTM_HIDDEN_SIZE; j++) {

        float sum_i = net->bi[j];
        float sum_f = net->bf[j];
        float sum_o = net->bo[j];
        float sum_g = net->bg[j];

        /* Input contribution: W * x */
        for (int k = 0; k < NUM_FEATURES; k++) {
            sum_i += net->Wi[j][k] * input[k];
            sum_f += net->Wf[j][k] * input[k];
            sum_o += net->Wo[j][k] * input[k];
            sum_g += net->Wg[j][k] * input[k];
        }

        /* Recurrent contribution: U * h */
        for (int k = 0; k < LSTM_HIDDEN_SIZE; k++) {
            sum_i += net->Ui[j][k] * net->h[k];
            sum_f += net->Uf[j][k] * net->h[k];
            sum_o += net->Uo[j][k] * net->h[k];
            sum_g += net->Ug[j][k] * net->h[k];
        }

        /* Apply activations */
        i_gate[j] = LSTM_Sigmoid(sum_i);
        f_gate[j] = LSTM_Sigmoid(sum_f);
        o_gate[j] = LSTM_Sigmoid(sum_o);
        g_gate[j] = LSTM_Tanh(sum_g);
    }

    /* ── Update cell and hidden states ───────────────────── */
    for (int j = 0; j < LSTM_HIDDEN_SIZE; j++) {
        c_new[j] = f_gate[j] * net->c[j]
                 + i_gate[j] * g_gate[j];
        h_new[j] = o_gate[j] * LSTM_Tanh(c_new[j]);
    }

    /* Copy new states back */
    memcpy(net->c, c_new, sizeof(float) * LSTM_HIDDEN_SIZE);
    memcpy(net->h, h_new, sizeof(float) * LSTM_HIDDEN_SIZE);

    /* ── Output layer: Y = Wy * h + by ──────────────────── */
    for (int i = 0; i < NUM_TARGETS; i++) {
        float sum = net->by[i];
        for (int j = 0; j < LSTM_HIDDEN_SIZE; j++) {
            sum += net->Wy[i][j] * net->h[j];
        }
        output[i] = sum;  /* Raw output (de-normalize externally) */
    }
}

/* ── Update Sequence Buffer (sliding window) ─────────────── */
void TMS_UpdateSequenceBuffer(TMSController_t *tms)
{
    SensorData_t *s = &tms->sensors;

    /* Build feature vector for current timestep */
    float features[NUM_FEATURES];
    features[0]  = s->temperature[0];    /* T_batt_avg */
    features[1]  = s->temperature[1];    /* T_batt_max */
    features[2]  = s->temperature[3];    /* T_motor */
    features[3]  = s->temperature[4];    /* T_inverter */
    features[4]  = s->temperature[5];    /* T_cabin */
    features[5]  = s->ambient_temp;      /* T_ambient */
    features[6]  = s->temperature[6];    /* T_coolant_in */
    features[7]  = s->soc;               /* SOC */
    features[8]  = s->battery_current;   /* I_batt */
    features[9]  = s->vehicle_speed;     /* V_vehicle */
    features[10] = s->motor_power;       /* P_motor */
    features[11] = s->charge_power;      /* Q_charge */
    features[12] = s->temp_forecast;     /* T_forecast */
    features[13] = s->gps_elevation;     /* Elevation */
    features[14] = s->gps_grade;         /* Grade */

    /* Normalize features to [0, 1] using known ranges */
    float feat_min[NUM_FEATURES] = {
        -20,  -20,  20,   20,   15,
        -30,  -20,  0.0,  -200, 0,
         0,    0,  -30,   0,   -10
    };
    float feat_max[NUM_FEATURES] = {
         60,   65,  140,  165,  35,
         50,   80,  1.0,  400,  150,
         150,  350,  50,  2000,  10
    };

    for (int i = 0; i < NUM_FEATURES; i++) {
        float range = feat_max[i] - feat_min[i];
        if (range > 0.0f)
            features[i] = (features[i] - feat_min[i]) / range;
        features[i] = fmaxf(0.0f, fminf(1.0f, features[i]));
    }

    /* Circular buffer insert */
    int idx = tms->sequence_index % SEQ_LENGTH;
    memcpy(tms->sequence_buffer[idx], features,
           sizeof(float) * NUM_FEATURES);
    tms->sequence_index++;
}

/* ── Run Full LSTM Prediction ────────────────────────────── */
void TMS_RunLSTMPrediction(TMSController_t *tms)
{
    /* Need at least SEQ_LENGTH samples to predict */
    if (tms->sequence_index < SEQ_LENGTH) return;

    float output[NUM_TARGETS];
    LSTM_ResetState(&tms->lstm_net);

    /* Feed entire sequence through LSTM */
    int start = tms->sequence_index % SEQ_LENGTH;
    for (int step = 0; step < SEQ_LENGTH; step++) {
        int idx = (start + step) % SEQ_LENGTH;
        LSTM_Forward(&tms->lstm_net,
                     tms->sequence_buffer[idx],
                     output);
    }

    /* De-normalize predictions back to physical units */
    float T_min[NUM_TARGETS] = { -20.0f,  20.0f,  20.0f,  15.0f };
    float T_max[NUM_TARGETS] = {  60.0f, 140.0f, 165.0f,  35.0f };

    for (int i = 0; i < NUM_TARGETS; i++) {
        output[i] = output[i] * (T_max[i] - T_min[i]) + T_min[i];
    }

    tms->T_batt_pred  = output[0];
    tms->T_motor_pred = output[1];
    tms->T_inv_pred   = output[2];
    tms->T_cabin_pred = output[3];
}