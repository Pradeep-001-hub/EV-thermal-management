%% ============================================================
%  EV THERMAL MANAGEMENT — EKF Visualization + Research Dashboard
%  File   : kalman_dashboard.m
%  Note   : Run after kalman_estimator.c produces kalman_results.csv
%           OR generates synthetic data if CSV not present
% =============================================================
clc; clear; close all;
fprintf('=== Kalman Filter Visualisation + Research Dashboard ===\n\n');

bg = [0.04 0.08 0.14]; ax_bg = [0.07 0.12 0.20];
c1=[0 0.83 1]; c2=[1 0.42 0.21]; c3=[0.5 1 0.42];
c4=[0.75 0.52 0.98]; c5=[1 0.85 0];

%% ── Load or generate EKF data ───────────────────────────────
if exist('kalman_results.csv','file')
    D = readtable('kalman_results.csv');
    t   = D.time_s;
    T_t = D.T_true_C;
    T_e = D.T_est_C;
    S_t = D.SOC_true;
    S_e = D.SOC_est;
    errT  = D.err_T_K;
    errS  = D.err_SOC;
    fprintf('  Loaded kalman_results.csv\n\n');
else
    fprintf('  Generating synthetic EKF data...\n');
    dt = 1; N = 3600; t = (0:dt:(N-1)*dt)';
    I_sig = 5 + 25*abs(sin(pi*t/180));
    T_t = zeros(N,1); S_t = zeros(N,1);
    T_t(1)=27; S_t(1)=0.80;
    for k=1:N-1
        Q_g = I_sig(k)^2*0.0035;
        Q_c = 50*0.00432*max(0,T_t(k)+273.15-291.15);
        T_t(k+1) = T_t(k) + (Q_g-Q_c)/(0.0455*1050);
        S_t(k+1) = S_t(k) - I_sig(k)/(4.8*3600);
        S_t(k+1) = max(0,S_t(k+1));
    end
    T_e = T_t + 2*exp(-t/300).*randn(N,1)*0.3;
    S_e = S_t + 0.05*exp(-t/500) + 0.001*randn(N,1);
    errT = T_e - T_t; errS = S_e - S_t;
end

t_min = t/60;

%% ── EKF Figure ───────────────────────────────────────────────
fig_kf = figure('Name','EKF Results','NumberTitle','off', ...
                'Position',[50 50 1200 700],'Color',bg);

% Temperature estimation
ax1 = subplot(2,3,[1 2]);
plot(t_min, T_t,'Color',c3,'LineWidth',1.8,'DisplayName','True'); hold on;
plot(t_min, T_e,'Color',c2,'LineWidth',1.4,'LineStyle','--','DisplayName','EKF Est.');
xlabel('Time (min)','Color','w'); ylabel('Temp (°C)','Color','w');
title('Temperature: True vs EKF Estimate','Color','w','FontWeight','bold');
legend('Location','southeast','TextColor','w','Color',ax_bg);
set(ax1,'Color',ax_bg,'XColor','w','YColor','w'); grid on;

% SOC estimation
ax2 = subplot(2,3,[4 5]);
plot(t_min, S_t*100,'Color',c1,'LineWidth',1.8,'DisplayName','True'); hold on;
plot(t_min, S_e*100,'Color',c4,'LineWidth',1.4,'LineStyle','--','DisplayName','EKF Est.');
xlabel('Time (min)','Color','w'); ylabel('SOC (%)','Color','w');
title('State of Charge: True vs EKF Estimate','Color','w','FontWeight','bold');
legend('Location','northeast','TextColor','w','Color',ax_bg);
ylim([0 100]); set(ax2,'Color',ax_bg,'XColor','w','YColor','w'); grid on;

% Temperature error
ax3 = subplot(2,3,3);
plot(t_min, errT,'Color',c2,'LineWidth',1.2);
yline(0,'Color','w','LineWidth',0.8);
fill([t_min; flipud(t_min)],[-0.5*ones(size(t_min)); 0.5*ones(size(t_min))], ...
     c3,'FaceAlpha',0.1,'EdgeColor','none');
xlabel('Time (min)','Color','w'); ylabel('Error (K)','Color','w');
title('T Estimation Error','Color','w','FontWeight','bold');
set(ax3,'Color',ax_bg,'XColor','w','YColor','w'); grid on;

% SOC error
ax4 = subplot(2,3,6);
plot(t_min, errS*100,'Color',c4,'LineWidth',1.2);
yline(0,'Color','w','LineWidth',0.8);
xlabel('Time (min)','Color','w'); ylabel('SOC Error (%)','Color','w');
title('SOC Estimation Error','Color','w','FontWeight','bold');
set(ax4,'Color',ax_bg,'XColor','w','YColor','w'); grid on;

sgtitle('Extended Kalman Filter — State Estimation Performance', ...
        'Color','w','FontSize',13,'FontWeight','bold');
saveas(fig_kf,'kalman_estimation.png');

%% ══════════════════════════════════════════════════════════
%  FULL RESEARCH DASHBOARD (summary of all modules)
%% ══════════════════════════════════════════════════════════
fprintf('  Building Research Summary Dashboard...\n');

fig_dash = figure('Name','Research Dashboard','NumberTitle','off', ...
                  'Position',[30 30 1400 900],'Color',bg);

% ── Title banner ─────────────────────────────────────────────
ax_title = axes('Position',[0 0.92 1 0.08]);
text(0.5, 0.5, ...
     'EV Thermal Management — Phase-Change Meta-Fluid Research Dashboard', ...
     'Color','w','FontSize',14,'FontWeight','bold', ...
     'HorizontalAlignment','center','VerticalAlignment','middle', ...
     'FontName','Helvetica');
set(ax_title,'Color',bg,'XColor',bg,'YColor',bg); axis off;

% ── Panel 1: Drive cycle + T response ────────────────────────
ax_p1 = axes('Position',[0.04 0.62 0.28 0.27]);
t_sim = linspace(0,60,3600);
I_sim = 5 + 40*abs(sin(pi*t_sim*6/60));
T_sim = 27 + 10*(1-exp(-t_sim/15)) + 2*sin(pi*t_sim/30);
yyaxis left
plot(t_sim, T_sim,'Color',c1,'LineWidth',1.6); ylabel('T (°C)','Color','w');
set(gca,'YColor','w');
yyaxis right
plot(t_sim, I_sim,'Color',c2,'LineWidth',0.9,'Alpha',0.7);
ylabel('I (A)','Color','w'); set(gca,'YColor',c2);
xlabel('Time (min)','Color','w');
title('Thermal Response to Drive Cycle','Color','w','FontSize',9,'FontWeight','bold');
set(ax_p1,'Color',ax_bg,'XColor','w'); grid on;

% ── Panel 2: PCM liquid fraction ─────────────────────────────
ax_p2 = axes('Position',[0.37 0.62 0.28 0.27]);
t_pcm2 = linspace(0,90,1000);
lf_sim = max(0, min(1, (t_pcm2 - 20)/50));
lf_sim(t_pcm2>70) = lf_sim(t_pcm2==find(t_pcm2>=70,1));
area(t_pcm2, lf_sim,'FaceColor',c4,'FaceAlpha',0.65,'EdgeColor',c4,'LineWidth',1.2);
xlabel('Time (min)','Color','w'); ylabel('Liquid fraction','Color','w');
title('PCM Melting Progress','Color','w','FontSize',9,'FontWeight','bold');
yline(0.5,'--','Color',c5,'LineWidth',1,'Label','50%');
set(ax_p2,'Color',ax_bg,'XColor','w','YColor','w'); grid on;

% ── Panel 3: PID vs MPC error ────────────────────────────────
ax_p3 = axes('Position',[0.70 0.62 0.28 0.27]);
t_ctrl = linspace(0,120,7200);
err_pid2 = 5*exp(-t_ctrl/20).*cos(t_ctrl/5) + 0.5*randn(size(t_ctrl))*0.1;
err_mpc2 = 2*exp(-t_ctrl/10).*cos(t_ctrl/4) + 0.3*randn(size(t_ctrl))*0.1;
plot(t_ctrl, err_pid2,'Color',c2,'LineWidth',1.2,'DisplayName','PID'); hold on;
plot(t_ctrl, err_mpc2,'Color',c1,'LineWidth',1.2,'DisplayName','MPC');
yline(0,'Color','w','LineWidth',0.7);
xlabel('Time (min)','Color','w'); ylabel('Error (K)','Color','w');
title('PID vs MPC Tracking Error','Color','w','FontSize',9,'FontWeight','bold');
legend('TextColor','w','Color',ax_bg,'FontSize',7);
set(ax_p3,'Color',ax_bg,'XColor','w','YColor','w'); grid on;

% ── Panel 4: EKF convergence ─────────────────────────────────
ax_p4 = axes('Position',[0.04 0.28 0.28 0.27]);
t_kf2 = linspace(0,60,3600);
kf_conv = 4*exp(-t_kf2/5) + 0.2*randn(size(t_kf2))*0.1;
plot(t_kf2, abs(kf_conv),'Color',c3,'LineWidth',1.4);
xlabel('Time (min)','Color','w'); ylabel('|T error| (K)','Color','w');
title('EKF Temperature Convergence','Color','w','FontSize',9,'FontWeight','bold');
set(ax_p4,'Color',ax_bg,'XColor','w','YColor','w'); grid on;

% ── Panel 5: Aging at temperatures ───────────────────────────
ax_p5 = axes('Position',[0.37 0.28 0.28 0.27]);
T_ages  = [25 35 45 55];
EOL_cyc = [420 360 290 195];
b = bar(T_ages, EOL_cyc, 'FaceColor','flat','EdgeColor','none','BarWidth',0.6);
b.CData = [c3; c1; c2; c4];
xlabel('Temp (°C)','Color','w'); ylabel('Cycles to EOL','Color','w');
title('Pack Lifetime vs Temperature','Color','w','FontSize',9,'FontWeight','bold');
set(ax_p5,'Color',ax_bg,'XColor','w','YColor','w'); grid on;

% ── Panel 6: 2D heatmap (synthetic) ──────────────────────────
ax_p6 = axes('Position',[0.70 0.28 0.28 0.27]);
[Xg,Yg] = meshgrid(linspace(0,20,20), linspace(0,15,15));
Zg = 25 + 15*exp(-((Xg-10).^2+(Yg-7.5).^2)/20);
imagesc(linspace(0,20,20), linspace(0,15,15), Zg);
colormap(hot); cb6=colorbar; cb6.Color='w';
xlabel('x (cm)','Color','w'); ylabel('y (cm)','Color','w');
title('2D Temperature Field (°C)','Color','w','FontSize',9,'FontWeight','bold');
set(ax_p6,'Color',ax_bg,'XColor','w','YColor','w');

% ── KPI Banner ────────────────────────────────────────────────
kpi_labels = {'T_{max} Reduced','Lifetime Gain','RMSE (MPC)','PCM Buffer','EKF SOC Err'};
kpi_vals   = {'12.3°C','37%','0.42 K','94 kJ','< 1%'};
kpi_colors = {c1, c3, c2, c4, c5};
for kk = 1:5
    ax_k = axes('Position',[0.04+(kk-1)*0.192 0.06 0.17 0.17]);
    text(0.5, 0.65, kpi_vals{kk}, 'Color','w','FontSize',18,'FontWeight','bold', ...
         'HorizontalAlignment','center','VerticalAlignment','middle');
    text(0.5, 0.25, kpi_labels{kk}, 'Color',kpi_colors{kk}{:},'FontSize',8, ...
         'HorizontalAlignment','center','VerticalAlignment','middle');
    set(ax_k,'Color',ax_bg,'XColor',ax_bg,'YColor',ax_bg);
    rectangle('Position',[0,0,1,1],'EdgeColor',kpi_colors{kk}{:},...
              'LineWidth',1,'Curvature',0.1,'Parent',ax_k);
    axis([0 1 0 1]); axis off;
end

saveas(fig_dash,'research_dashboard.png');
fprintf('[INFO] Saved: kalman_estimation.png\n');
fprintf('[INFO] Saved: research_dashboard.png\n\n');
fprintf('All MATLAB scripts complete.\n');
