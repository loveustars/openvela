/**
 * @file local_main.c
 * @brief PC 端本地测试入口
 *
 * 用于在部署到 OpenVela 板子前验证信号控制器逻辑。
 * 支持多种场景仿真，无需 MQTT 连接。
 *
 * @author OpenVela Traffic Team
 * @version 1.1.0
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>

#include "signal_controller.h"
#include "simulator.h"

#define VERSION       "1.1.0"
#define MAX_VEHICLES  128

/** @brief 全局运行标志 */
static volatile int g_running = 1;

/**
 * @brief 信号处理函数
 */
static void signal_handler(int sig) {
    (void)sig;
    printf("\n[MAIN] Shutting down...\n");
    g_running = 0;
}

/**
 * @brief 打印帮助信息
 */
static void print_help(const char *prog) {
    printf("用法: %s [选项]\n", prog);
    printf("选项:\n");
    printf("  --scenario <name>  场景 (balanced/ns_rush/ew_rush/left_rush/alternating_wave/idle)\n");
    printf("  --duration <sec>   仿真时长(秒)\n");
    printf("  --demo             演示模式\n");
    printf("  --verbose          详细输出\n");
    printf("  -h, --help         帮助\n");
}

/**
 * @brief 打印信号决策摘要
 */
static void print_decision(const SignalDecision *dec, int tick, int verbose) {
    const char *phase_str = phase_to_string(dec->phase);

    /* 车道描述 */
    const char *lane_desc;
    switch (dec->allowed_lanes_mask & ~0x04) { /* 排除右转（始终允许）*/
    case 0x02:
        lane_desc = "(直行+右转通行)";
        break;
    case 0x01:
        lane_desc = "(左转+右转通行)";
        break;
    case 0x00:
        lane_desc = "(仅右转通行)";
        break;
    default:
        lane_desc = "";
        break;
    }

    printf("[t=%04ds] %-10s dur=%2d elap=%2d %s | %s\n",
           tick, phase_str, dec->duration, dec->phase_elapsed,
           lane_desc, dec->reason);

    if (verbose) {
        printf("  走廊统计: NS=%d EW=%d | 等待: NS=%d EW=%d\n",
               dec->corridor_counts[0], dec->corridor_counts[1],
               dec->waiting_seconds[0], dec->waiting_seconds[1]);
        printf("  队列(左/直/右):\n");
        for (int r = 0; r < 4; r++) {
            printf("    %s: %d/%d/%d\n", road_to_string(r),
                   dec->road_lanes[r][0].total,
                   dec->road_lanes[r][1].total,
                   dec->road_lanes[r][2].total);
        }
    }
}

/**
 * @brief 运行单场景仿真
 */
static int run_simulation(ScenarioType scenario, int duration, int verbose) {
    printf("\n");
    printf("╔═══════════════════════════════════════════════════════╗\n");
    printf("║   Traffic Controller v%s (三车道版)                  ║\n", VERSION);
    printf("║   右转始终允许 | 左转保护三相位                       ║\n");
    printf("╚═══════════════════════════════════════════════════════╝\n");
    printf("\n场景: %s\n", simulator_scenario_name(scenario));
    printf("     %s\n", simulator_scenario_desc(scenario));
    printf("时长: %d秒\n\n", duration);

    SimulatorConfig sim_cfg = {
        .scenario = scenario,
        .duration_seconds = duration,
        .tick_interval_ms = 1000
    };
    Simulator *sim = simulator_create(&sim_cfg);
    TrafficController ctrl;
    controller_init(&ctrl, NULL, PHASE_NS_STRAIGHT);

    SimVehicle sim_vehicles[MAX_VEHICLES];
    SignalPhase last_phase = PHASE_NS_STRAIGHT;

    int tick = 0;
    while (g_running && tick < duration) {
        int vehicle_count = simulator_step(sim, sim_vehicles, MAX_VEHICLES);

        /* 转换为控制器输入格式 */
        VehicleState vehicles[MAX_VEHICLES];
        for (int i = 0; i < vehicle_count; i++) {
            vehicles[i].road = sim_vehicles[i].road;
            vehicles[i].lane = sim_vehicles[i].lane;
            vehicles[i].position = sim_vehicles[i].position;
            vehicles[i].speed = sim_vehicles[i].speed;
            vehicles[i].is_waiting = sim_vehicles[i].is_waiting;
            strncpy(vehicles[i].vehicle_id, sim_vehicles[i].vehicle_id,
                    sizeof(vehicles[i].vehicle_id) - 1);
        }

        SignalDecision dec = controller_step(&ctrl, vehicles, vehicle_count);

        /* 相位变化时打印切换信息 */
        if (dec.phase != last_phase) {
            printf("\n>>> 相位切换: %s → %s <<<\n",
                   phase_to_string(last_phase), phase_to_string(dec.phase));
            last_phase = dec.phase;
        }

        /* 按相位放行车辆（实际通行判断见 controller_can_pass） */
        simulator_discharge(sim, dec.phase, ctrl.config.release_rate_per_road);

        print_decision(&dec, tick, verbose);

        tick++;
    }

    /* 最终队列状态统计 */
    int queues[4][3];
    simulator_get_lane_queues(sim, queues);
    printf("\n═══════════════════════════════════════════════════════\n");
    printf("模拟结束 - 最终队列状态:\n");
    printf("  %-4s | %-10s %-10s %-10s %-10s\n", "", "N", "S", "E", "W");
    printf("  ───────────────────────────────────────────────\n");
    printf("  %-4s | %-10d %-10d %-10d %-10d\n", "左转",
           queues[0][0], queues[1][0], queues[2][0], queues[3][0]);
    printf("  %-4s | %-10d %-10d %-10d %-10d\n", "直行",
           queues[0][1], queues[1][1], queues[2][1], queues[3][1]);
    printf("  %-4s | %-10d %-10d %-10d %-10d\n", "右转",
           queues[0][2], queues[1][2], queues[2][2], queues[3][2]);
    printf("═══════════════════════════════════════════════════════\n");
    printf("\n统计: 共运行 %d ticks, 相位切换 %d 次\n",
           ctrl.tick_count, ctrl.switch_count);

    simulator_destroy(sim);
    return 0;
}

/**
 * @brief 演示模式：循环所有场景
 */
static int run_demo(void) {
    printf("\n========== 演示模式: 循环所有场景 ==========\n");

    for (int i = 0; i < SCENARIO_COUNT && g_running; i++) {
        printf("\n\n");
        printf("##################################################\n");
        printf("## 场景 %d/%d: %s\n",
               i + 1, SCENARIO_COUNT, simulator_scenario_name((ScenarioType)i));
        printf("## %s\n", simulator_scenario_desc((ScenarioType)i));
        printf("##################################################\n");

        run_simulation((ScenarioType)i, 15, 0);
    }

    return 0;
}

/**
 * @brief 主函数入口
 */
int main(int argc, char *argv[]) {
    ScenarioType scenario = SCENARIO_BALANCED;
    int duration = 30;
    int verbose = 0;
    int demo_mode = 0;

    signal(SIGINT, signal_handler);

    /* 参数解析 */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--scenario") == 0 && i + 1 < argc) {
            i++;
            if (strcmp(argv[i], "balanced") == 0) scenario = SCENARIO_BALANCED;
            else if (strcmp(argv[i], "ns_rush") == 0) scenario = SCENARIO_NS_RUSH;
            else if (strcmp(argv[i], "ew_rush") == 0) scenario = SCENARIO_EW_RUSH;
            else if (strcmp(argv[i], "left_rush") == 0) scenario = SCENARIO_LEFT_RUSH;
            else if (strcmp(argv[i], "alternating_wave") == 0) scenario = SCENARIO_ALTERNATING_WAVE;
            else if (strcmp(argv[i], "idle") == 0) scenario = SCENARIO_IDLE;
        } else if (strcmp(argv[i], "--duration") == 0 && i + 1 < argc) {
            duration = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--verbose") == 0) {
            verbose = 1;
        } else if (strcmp(argv[i], "--demo") == 0) {
            demo_mode = 1;
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_help(argv[0]);
            return 0;
        }
    }

    if (demo_mode) {
        return run_demo();
    } else {
        return run_simulation(scenario, duration, verbose);
    }
}