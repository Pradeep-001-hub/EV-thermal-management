%% ============================================================
%  EV THERMAL MANAGEMENT — PID vs MPC Controller Comparison
%  File   : pid_mpc_controller_comparison.m
%  Note   : MPC implemented manually (no MPC Toolbox needed)
% =============================================================
clc; clear; close all;
fprintf('=== PID vs MPC Thermal Controller Comparison ===\n\n');

%% ── PLANT PARAMETERS ────────────────────────────────────────
m_pack  = 4.5;       % kg  battery pack mass
Cp_pack = 1050;      % J/(kg·K)
A_cool  = 0.12;      % m²  cooling area
T_cool  = 291.15;    % K   coolant inlet
T_amb   = 298.15;    % K

% Cooling: Q_cool = mdot * Cp_f * (T - T_in)
Cp_fluid = 3800;     % J/(kg·K)  50:50 glycol-water

% Inputs / disturbances
dt   = 1;            % s
t_end = 7200;        % s  2-hour
time = (0:dt:t_end);
N    = length(time);

T_sp = 308.15;       % K  setpoint 35°C

%% ── DISTURBANCE: heat generation ────────────────────────────
Q_dist = zeros(1, N);
for k = 1:N
    cyc = mod(time(k), 600);
    if cyc < 200,      Q_dist(k) = 180 + 60*sin(pi*cyc/200);
    elseif cyc < 400,  Q_dist(k) = 80  + 20*sin(pi*cyc/100);
    else,              Q_dist(k) = 250 * abs(sin(pi*cyc/200));
    end
end

%% ── SECTION A: PID CONTROLLER ───────────────────────────────
fprintf('  Running PID controller...\n');

% PID gains (tuned via Ziegler-Nichols + manual refinement)
Kp = 0.008;  Ki = 0.00005;  Kd = 0.002;

T_pid  = zeros(1, N);   T_pid(1)  = T_amb;
mdot_pid = zeros(1, N); mdot_pid(1) = 0.005;
u_int  = 0;
u_prev_err = 0;
MDOT_MIN = 0.001;  MDOT_MAX = 0.05;

for k = 1:N-1
    T = T_pid(k);
    err = T_sp - T;
    if abs(err) < 1.0, err = 0; end   % deadband

    u_int = u_int + err*dt;
    u_int = max(-0.02, min(0.02, u_int));  % anti-windup

    u_der = (err - u_prev_err)/dt;
    u_prev_err = err;

    mdot = Kp*err + Ki*u_int + Kd*u_der;
    mdot = max(MDOT_MIN, min(MDOT_MAX, mdot));
    mdot_pid(k+1) = mdot;

    Q_cool = mdot * Cp_fluid * max(0, T - T_cool);
    dT = (Q_dist(k) - Q_cool) / (m_pack * Cp_pack) * dt;
    T_pid(k+1) = T + dT;
end

%% ── SECTION B: LINEAR MPC (Prediction Horizon Hp=20) ────────
fprintf('  Running MPC controller...\n');

Hp   = 20;   % prediction horizon (steps)
Hc   = 5;    % control horizon
Q_w  = 100;  % output tracking weight
R_w  = 1;    % control effort weight

% Linear state-space of plant: T_{k+1} = a*T_k + b*u_k + d*Q_dist
% u = mdot (kg/s),  d = disturbance coefficient
tau_th = m_pack * Cp_pack;    % J/K
a_ss   = 1 - (Cp_fluid * MDOT_MIN) / tau_th * dt;  % approx linearised
b_ss   = -Cp_fluid * dt / tau_th;                   % control gain (negative: more mdot => lower T)
d_ss   =  dt / tau_th;                              % disturbance gain

T_mpc    = zeros(1, N);   T_mpc(1)  = T_amb;
mdot_mpc = zeros(1, N);   mdot_mpc(1) = 0.005;
u_mpc_prev = 0.005;

for k = 1:N-1
    T = T_mpc(k);
    Q_d = Q_dist(k);

    % Build prediction matrices (simplified, constant u over Hc)
    % Minimise J = sum(Q_w*(T_pred - T_sp)^2) + R_w*(du)^2
    % Unconstrained first-order MPC analytical solution:
    % For Hp=1 simplification (receding horizon):
    T_pred = zeros(1, Hp);
    u_seq  = u_mpc_prev * ones(1, Hc);

    % Numerator/denominator for optimal du
    num_du = 0; den_du = 0;
    T_curr = T;
    for p = 1:Hp
        u_p = u_seq(min(p, Hc));
        T_next_pred = a_ss*T_curr + b_ss*u_p + d_ss*Q_d;
        num_du = num_du + Q_w * b_ss * (T_sp - T_next_pred);
        den_du = den_du + Q_w * b_ss^2;
        T_curr = T_next_pred;
    end
    den_du = den_du + R_w;

    du_opt = num_du / den_du;
    u_new  = u_mpc_prev + du_opt;
    u_new  = max(MDOT_MIN, min(MDOT_MAX, u_new));
    mdot_mpc(k+1) = u_new;
    u_mpc_prev = u_new;

    % Apply to real plant (nonlinear)
    Q_cool = u_new * Cp_fluid * max(0, T - T_cool);
    dT = (Q_dist(k) - Q_cool) / (m_pack * Cp_pack) * dt;
    T_mpc(k+1) = T + dT;
end

%% ── PERFORMANCE METRICS ─────────────────────────────────────
rmse_pid = sqrt(mean((T_pid - T_sp).^2));
rmse_mpc = sqrt(mean((T_mpc - T_sp).^2));

energy_pid = sum(mdot_pid) * dt * Cp_fluid * mean(max(0, T_pid - T_cool));
energy_mpc = sum(mdot_mpc) * dt * Cp_fluid * mean(max(0, T_mpc - T_cool));

fprintf('\n  %-22s %-12s %-12s\n', 'Metric','PID','MPC');
fprintf('  %-22s %-12s\n', repmat('-',1,46));
fprintf('  %-22s %-12.4f %-12.4f\n', 'RMSE (K)',        rmse_pid,   rmse_mpc);
fprintf('  %-22s %-12.2f %-12.2f\n', 'T_max (°C)',      max(T_pid)-273.15, max(T_mpc)-273.15);
fprintf('  %-22s %-12.2f %-12.2f\n', 'T_mean (°C)',     mean(T_pid)-273.15, mean(T_mpc)-273.15);
fprintf('  %-22s %-12.5f %-12.5f\n', 'mdot_mean (kg/s)',mean(mdot_pid), mean(mdot_mpc));
fprintf('\n');

%% ── PLOTS ────────────────────────────────────────────────────
t_min = time/60;
bg    = [0.04 0.08 0.14];
ax_bg = [0.07 0.12 0.20];
c_pid = [1.00 0.42 0.21];
c_mpc = [0.00 0.83 1.00];
c_sp  = [0.50 1.00 0.42];

fig = figure('Name','PID vs MPC','NumberTitle','off', ...
             'Position',[80 60 1200 780],'Color',bg);

% ── Temperature comparison ──────────────────────────────────
ax1 = subplot(2,2,[1 2]);
plot(t_min, T_pid-273.15, 'Color',c_pid, 'LineWidth',1.8,'DisplayName','PID'); hold on;
plot(t_min, T_mpc-273.15, 'Color',c_mpc, 'LineWidth',1.8,'DisplayName','MPC');
yline(T_sp-273.15,'--','Color',c_sp,'LineWidth',1.4,'Label','Setpoint 35°C');
yline(60,'--','Color',[1 0.2 0.2],'LineWidth',0.9,'Label','Safety 60°C');
xlabel('Time (min)','Color','w'); ylabel('Temperature (°C)','Color','w');
title('Temperature Response: PID vs MPC','Color','w','FontWeight','bold','FontSize',12);
legend('Location','northeast','TextColor','w','Color',ax_bg);
set(ax1,'Color',ax_bg,'XColor','w','YColor','w','GridColor',[0.3 0.3 0.3],'GridAlpha',0.3);
grid on;

% ── Control effort (flow rate) ──────────────────────────────
ax2 = subplot(2,2,3);
plot(t_min, mdot_pid*1000,'Color',c_pid,'LineWidth',1.6,'DisplayName','PID'); hold on;
plot(t_min, mdot_mpc*1000,'Color',c_mpc,'LineWidth',1.6,'DisplayName','MPC');
xlabel('Time (min)','Color','w'); ylabel('Flow rate (g/s)','Color','w');
title('Coolant Flow Rate','Color','w','FontWeight','bold');
legend('Location','northeast','TextColor','w','Color',ax_bg);
set(ax2,'Color',ax_bg,'XColor','w','YColor','w','GridColor',[0.3 0.3 0.3],'GridAlpha',0.3);
grid on;

% ── Tracking error ──────────────────────────────────────────
ax3 = subplot(2,2,4);
plot(t_min, T_pid-T_sp,'Color',c_pid,'LineWidth',1.4,'DisplayName','PID error'); hold on;
plot(t_min, T_mpc-T_sp,'Color',c_mpc,'LineWidth',1.4,'DisplayName','MPC error');
yline(0,'Color','w','LineWidth',0.8);
fill([t_min fliplr(t_min)],[-1 -1 1 1]*2, [0.5 1 0.5],'FaceAlpha',0.08,'EdgeColor','none');
xlabel('Time (min)','Color','w'); ylabel('Error (K)','Color','w');
title('Temperature Tracking Error','Color','w','FontWeight','bold');
legend('Location','northeast','TextColor','w','Color',ax_bg);
set(ax3,'Color',ax_bg,'XColor','w','YColor','w','GridColor',[0.3 0.3 0.3],'GridAlpha',0.3);
grid on;

sgtitle('EV Thermal Control: PID vs MPC Performance', ...
        'Color','w','FontSize',13,'FontWeight','bold');

saveas(fig,'pid_mpc_comparison.png');
fprintf('[INFO] Figure saved: pid_mpc_comparison.png\n');

%% ── EXPORT ──────────────────────────────────────────────────
T_table = table(time', T_pid'-273.15, T_mpc'-273.15, ...
                mdot_pid'*1000, mdot_mpc'*1000, Q_dist', ...
    'VariableNames',{'time_s','T_PID_C','T_MPC_C', ...
                     'mdot_PID_gs','mdot_MPC_gs','Q_dist_W'});
writetable(T_table,'pid_mpc_results.csv');
fprintf('[INFO] Data saved: pid_mpc_results.csv\n\n');
