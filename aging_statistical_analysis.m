%% ============================================================
%  EV THERMAL MANAGEMENT — SOC-OCV, Aging & Statistical Analysis
%  File   : aging_statistical_analysis.m
%  MATLAB : R2021b+  (no toolboxes needed)
% =============================================================
clc; clear; close all;
fprintf('=== Aging & Statistical Thermal Analysis ===\n\n');

bg = [0.04 0.08 0.14]; ax_bg = [0.07 0.12 0.20];
c1=[0 0.83 1]; c2=[1 0.42 0.21]; c3=[0.5 1 0.42]; c4=[0.75 0.52 0.98];
c5=[1 0.85 0];

%% ══════════════════════════════════════════════════════════
%  PART A: SOC–OCV POLYNOMIAL MODEL
%% ══════════════════════════════════════════════════════════

SOC_pts  = [0 0.1 0.2 0.3 0.4 0.5 0.6 0.7 0.8 0.9 1.0];
OCV_pts  = [3.00 3.35 3.50 3.58 3.63 3.70 3.80 3.90 4.00 4.10 4.20];

% 5th-order polynomial fit
p_ocv = polyfit(SOC_pts, OCV_pts, 5);

SOC_fine = linspace(0, 1, 200);
OCV_fit  = polyval(p_ocv, SOC_fine);

% dOCV/dSOC  (incremental capacity analysis)
dOCV_dSOC = polyder(p_ocv);
dOCV_fine = polyval(dOCV_dSOC, SOC_fine);

fprintf('  OCV polynomial coefficients (degree 5):\n');
fprintf('  '); fprintf('%.6f  ', p_ocv); fprintf('\n\n');

%% ── OCV Plots ────────────────────────────────────────────────
fig_ocv = figure('Name','SOC-OCV','NumberTitle','off', ...
                 'Position',[50 50 1000 440],'Color',bg);

ax1 = subplot(1,2,1);
plot(SOC_fine*100, OCV_fit,'Color',c1,'LineWidth',2,'DisplayName','Poly fit'); hold on;
scatter(SOC_pts*100, OCV_pts, 60, c2,'filled','DisplayName','Measured');
xlabel('SOC (%)','Color','w'); ylabel('OCV (V)','Color','w');
title('OCV–SOC Characteristic','Color','w','FontWeight','bold');
legend('Location','northwest','TextColor','w','Color',ax_bg);
set(ax1,'Color',ax_bg,'XColor','w','YColor','w'); grid on;

ax2 = subplot(1,2,2);
plot(SOC_fine*100, dOCV_fine,'Color',c3,'LineWidth',2);
xlabel('SOC (%)','Color','w'); ylabel('dOCV/dSOC (V)','Color','w');
title('Incremental Capacity Analysis','Color','w','FontWeight','bold');
set(ax2,'Color',ax_bg,'XColor','w','YColor','w'); grid on;

sgtitle('SOC–OCV Model & Incremental Capacity','Color','w','FontSize',12);
saveas(fig_ocv,'soc_ocv_model.png');

%% ══════════════════════════════════════════════════════════
%  PART B: CAPACITY FADE & THERMAL AGING MODEL
%% ══════════════════════════════════════════════════════════
% Arrhenius-based capacity fade:
%   Q_fade = A * exp(-Ea / (R*T)) * sqrt(Ah_throughput)
% Reference: Wang et al. (2011), J. Power Sources

Ea   = 31500;       % J/mol  activation energy
R_gas = 8.314;      % J/(mol·K)
A_fade = 14876;     % pre-exponential
Q_nom  = 4.8;       % Ah nominal capacity

% Simulate 500 cycles at different temperatures
n_cycles = 500;
T_ops = [25 35 45 55] + 273.15;   % K
colors_age = {c3, c1, c2, c4};

cap_fade = zeros(length(T_ops), n_cycles);
cycles   = 1:n_cycles;

for ti = 1:length(T_ops)
    T_op = T_ops(ti);
    for cyc = 1:n_cycles
        Ah_thru = cyc * Q_nom * 2;   % full charge-discharge
        k_fade  = A_fade * exp(-Ea / (R_gas * T_op));
        Q_loss  = k_fade * sqrt(Ah_thru);     % %  
        cap_fade(ti, cyc) = max(0, Q_nom * (1 - Q_loss/100));
    end
end

% End-of-life at 80% capacity
EOL_cycle = zeros(1, length(T_ops));
for ti = 1:length(T_ops)
    idx = find(cap_fade(ti,:) < 0.80*Q_nom, 1, 'first');
    if ~isempty(idx), EOL_cycle(ti) = idx; else, EOL_cycle(ti) = n_cycles; end
end

fprintf('  Capacity Fade — End-of-Life Cycles:\n');
for ti = 1:length(T_ops)
    fprintf('    T = %d°C :  EOL at cycle %d\n', ...
            round(T_ops(ti)-273.15), EOL_cycle(ti));
end
fprintf('\n');

%% ── Aging Plots ─────────────────────────────────────────────
fig_age = figure('Name','Aging Analysis','NumberTitle','off', ...
                 'Position',[80 60 1100 700],'Color',bg);

ax_a = subplot(2,2,[1 2]);
for ti = 1:length(T_ops)
    plot(cycles, cap_fade(ti,:)/Q_nom*100, ...
         'Color',colors_age{ti}{:},'LineWidth',1.8, ...
         'DisplayName',sprintf('%d°C', round(T_ops(ti)-273.15))); hold on;
end
yline(80,'--','Color','w','LineWidth',1.2,'Label','EOL = 80%');
xlabel('Cycle number','Color','w'); ylabel('Capacity retention (%)','Color','w');
title('Capacity Fade vs Temperature (Arrhenius Model)','Color','w','FontWeight','bold','FontSize',11);
legend('Location','southwest','TextColor','w','Color',ax_bg);
ylim([60 101]);
set(ax_a,'Color',ax_bg,'XColor','w','YColor','w'); grid on;

ax_b = subplot(2,2,3);
T_range = 20:5:60;
k_rate  = A_fade * exp(-Ea ./ (R_gas * (T_range+273.15)));
semilogy(T_range, k_rate, 'Color',c5,'LineWidth',2,'Marker','o',...
         'MarkerFaceColor',c5,'MarkerSize',5);
xlabel('Temperature (°C)','Color','w'); ylabel('Fade rate constant','Color','w');
title('Arrhenius Rate Constant vs T','Color','w','FontWeight','bold');
set(ax_b,'Color',ax_bg,'XColor','w','YColor','w'); grid on;

ax_c = subplot(2,2,4);
bar(T_ops-273.15, EOL_cycle, 'FaceColor',c4,'EdgeColor','none','FaceAlpha',0.8);
xlabel('Operating Temp (°C)','Color','w'); ylabel('EOL Cycle','Color','w');
title('Pack Lifetime vs Operating Temperature','Color','w','FontWeight','bold');
set(ax_c,'Color',ax_bg,'XColor','w','YColor','w'); grid on;

sgtitle('Thermal Aging Analysis — Arrhenius Capacity Fade Model', ...
        'Color','w','FontSize',12,'FontWeight','bold');
saveas(fig_age,'aging_analysis.png');

%% ══════════════════════════════════════════════════════════
%  PART C: MONTE CARLO — TEMPERATURE UNCERTAINTY ANALYSIS
%% ══════════════════════════════════════════════════════════
fprintf('  Running Monte Carlo uncertainty analysis...\n');

n_mc  = 2000;
dt_mc = 1;
t_mc  = 0:dt_mc:3600;
Nm    = length(t_mc);

% Uncertain parameters (normal distribution)
mc_mass   = 0.0455 + 0.003*randn(n_mc,1);
mc_Cp     = 1050   + 50*randn(n_mc,1);
mc_R_int  = 0.0035 + 0.0003*randn(n_mc,1);
mc_h      = 150    + 20*randn(n_mc,1);

T_peaks_mc = zeros(n_mc, 1);
T_final_mc = zeros(n_mc, 1);

parfor_flag = false;  % set true if Parallel Toolbox available

for mc = 1:n_mc
    T_mc = 300.15;
    for k = 1:Nm-1
        t_k = t_mc(k);
        I_k = 5 + 40*abs(sin(pi*t_k/180));
        Q_g = I_k^2 * mc_R_int(mc);
        Q_c = mc_h(mc) * 0.00432 * max(0, T_mc - 291.15);
        T_mc = T_mc + (Q_g - Q_c)/(mc_mass(mc)*mc_Cp(mc))*dt_mc;
    end
    T_peaks_mc(mc) = T_mc;
    T_final_mc(mc) = T_mc;
end

T_peak_C = T_peaks_mc - 273.15;
fprintf('  Monte Carlo (n=%d): T_mean=%.2f°C, T_std=%.3f K, '...
        'T_99th=%.2f°C\n\n', n_mc, mean(T_peak_C), std(T_peak_C), ...
        prctile(T_peak_C,99));

%% ── Monte Carlo Plots ────────────────────────────────────────
fig_mc = figure('Name','Monte Carlo','NumberTitle','off', ...
                'Position',[100 50 1000 420],'Color',bg);

ax_m1 = subplot(1,2,1);
histogram(T_peak_C, 50, 'FaceColor',c1,'FaceAlpha',0.75,'EdgeColor','none');
xline(mean(T_peak_C),'--','Color',c2,'LineWidth',1.8,'Label',sprintf('Mean %.1f°C',mean(T_peak_C)));
xline(prctile(T_peak_C,99),'--','Color',[1 0.2 0.2],'LineWidth',1.4,'Label','99th pct');
xlabel('Peak Temperature (°C)','Color','w'); ylabel('Count','Color','w');
title('MC Distribution of Peak Temperature','Color','w','FontWeight','bold');
set(ax_m1,'Color',ax_bg,'XColor','w','YColor','w'); grid on;

ax_m2 = subplot(1,2,2);
qqplot(T_peak_C);
set(findobj(ax_m2,'Type','line'),'Color',c1,'LineWidth',1.4);
xlabel('Standard Normal Quantiles','Color','w');
ylabel('Sample Quantiles (°C)','Color','w');
title('Q–Q Plot (Normality Check)','Color','w','FontWeight','bold');
set(ax_m2,'Color',ax_bg,'XColor','w','YColor','w'); grid on;

sgtitle('Monte Carlo Uncertainty — Peak Temperature','Color','w','FontSize',12);
saveas(fig_mc,'monte_carlo_analysis.png');

%% ── Export all data ─────────────────────────────────────────
save('ev_thermal_workspace.mat', 'SOC_fine','OCV_fit','dOCV_fine', ...
     'cap_fade','cycles','T_peak_C','T_ops','EOL_cycle');
fprintf('[INFO] Saved: soc_ocv_model.png, aging_analysis.png, monte_carlo_analysis.png\n');
fprintf('[INFO] Workspace: ev_thermal_workspace.mat\n\n');
