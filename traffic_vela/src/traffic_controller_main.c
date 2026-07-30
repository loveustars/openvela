/**
 * @file traffic_controller_main.c
 * @brief 智能交通信号控制器 - OpenVela 应用入口 (v2.0)
 *
 * 负责 B 的工作：
 * - 比较四个方向谁更着急
 * - 结合 A 计算的时间，判断是否该切换信号灯
 * - 管理黄灯、全红灯等安全过渡
 * - 处理通信异常等意外情况
 *
 * v2.0: 4 相位独立设计（NS_STRAIGHT / NS_LEFT / EW_STRAIGHT / EW_LEFT）
 *
 * 使用方法:
 *   nsh> traffic_controller --sim
 *   nsh> traffic_controller --scenario balanced
 *   nsh> traffic_controller --scenario ns_rush --duration 120
 *   nsh> traffic_controller --demo
 *
 *   命令行参数:
 *   --scenario <name>  场景: balanced/ns_rush/ew_rush/left_rush/alternating_wave/idle
 *   --duration <sec>   仿真时长(秒)
 *   --sim               启用模拟模式(不依赖 MQTT)
 *   --demo              演示模式(循环播放各场景)
 *   --config <json>    JSON格式配置参数
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>
#include <errno.h>
#include <sys/boardctl.h>

/* lvgldemo 的条件编译 — 如果系统没自动初始化, 我们手动初始化 */
#if defined(CONFIG_BOARDCTL) && !defined(CONFIG_NSH_ARCHINIT)
#  define NEED_BOARDINIT 1
#endif

/* FAR is a NuttX/OpenVela macro for far pointers (historical from 8086).
 * Define it as empty for PC builds (where it's not needed). */
#ifndef FAR
#define FAR
#endif

#include "signal_controller.h"
#include "comm.h"
#include "simulator.h"
#include "gui_lvgl.h"

/* 宏定义 */
#define VERSION             "2.0.0"
#define DEFAULT_SCENARIO    SCENARIO_BALANCED
#define DEFAULT_DURATION    120
#define MAX_VEHICLES        128
#define MQTT_PUBLISH_TOPIC  "v2i/spat/rsu_a"
#define MQTT_SUBSCRIBE_TOPIC "v2i/vehicle/data"

/* 全局运行状态 */
static volatile bool g_running = true;

/* 信号处理 */
static void signal_handler(int sig) {
    (void)sig;
    printf("\n[MAIN] Received signal, shutting down...\n");
    g_running = false;
}

/* 打印帮助信息 */
static void print_help(const char *prog) {
    printf("Usage: %s [options]\n", prog);
    printf("Options:\n");
    printf("  --scenario <name>  Simulation scenario\n");
    printf("  --duration <sec>   Simulation duration in seconds (default: %d)\n", DEFAULT_DURATION);
    printf("  --sim              Enable simulation mode (no real sensors/MQTT)\n");
    printf("  --demo             Demo mode: cycle through all scenarios\n");
    printf("  --mqtt-host <ip>   MQTT broker host\n");
    printf("  --mqtt-port <port> MQTT broker port\n");
    printf("  --verbose          Verbose output\n");
    printf("  -h, --help         Show this help\n");
    printf("\nScenarios:\n");
    for (int i = 0; i < SCENARIO_COUNT; i++) {
        printf("  %-20s - %s\n",
               simulator_scenario_name((ScenarioType)i),
               simulator_scenario_desc((ScenarioType)i));
    }
}

/* 解析命令行参数 */
typedef struct {
    ScenarioType  scenario;
    int           duration;
    bool sim_mode;
    bool          demo_mode;
    bool          verbose;
    char mqtt_host[64];
    int           mqtt_port;
    char          rsu_id[32];
} AppConfig;

static void init_config(AppConfig *cfg) {
    memset(cfg, 0, sizeof(AppConfig));
    cfg->scenario = DEFAULT_SCENARIO;
    cfg->duration = DEFAULT_DURATION;
    cfg->mqtt_port = 1883;
    strncpy(cfg->mqtt_host, "192.168.1.100", sizeof(cfg->mqtt_host) - 1);
    strncpy(cfg->rsu_id, "rsu_a", sizeof(cfg->rsu_id) - 1);
}

static int parse_args(AppConfig *cfg, int argc, char **argv) {
    init_config(cfg);

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--scenario") == 0 && i + 1 < argc) {
            i++;
            if (strcmp(argv[i], "balanced") == 0)        cfg->scenario = SCENARIO_BALANCED;
            else if (strcmp(argv[i], "ns_rush") == 0)    cfg->scenario = SCENARIO_NS_RUSH;
            else if (strcmp(argv[i], "ew_rush") == 0)    cfg->scenario = SCENARIO_EW_RUSH;
            else if (strcmp(argv[i], "left_rush") == 0)  cfg->scenario = SCENARIO_LEFT_RUSH;
            else if (strcmp(argv[i], "alternating_wave") == 0) cfg->scenario = SCENARIO_ALTERNATING_WAVE;
            else if (strcmp(argv[i], "idle") == 0)       cfg->scenario = SCENARIO_IDLE;
            else {
                printf("Error: unknown scenario '%s'\n", argv[i]);
                return -1;
            }
        } else if (strcmp(argv[i], "--duration") == 0 && i + 1 < argc) {
            cfg->duration = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--sim") == 0) {
            cfg->sim_mode = true;
        } else if (strcmp(argv[i], "--demo") == 0) {
            cfg->demo_mode = true;
        } else if (strcmp(argv[i], "--mqtt-host") == 0 && i + 1 < argc) {
            strncpy(cfg->mqtt_host, argv[++i], sizeof(cfg->mqtt_host) - 1);
        } else if (strcmp(argv[i], "--mqtt-port") == 0 && i + 1 < argc) {
            cfg->mqtt_port = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--verbose") == 0) {
            cfg->verbose = true;
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            return -1;
        }
    }

    return 0;
}

/* 打印信号决策摘要 */
static void print_decision(const SignalDecision *dec, int tick, bool verbose) {
    const char *phase_str = phase_to_string(dec->phase);
    const char *phase_emoji = "";

    switch (dec->phase) {
    case PHASE_NS_STRAIGHT: phase_emoji = "[NS_STRAIGHT 🚦]"; break;
    case PHASE_NS_LEFT:     phase_emoji = "[NS_LEFT 🚦]";     break;
    case PHASE_EW_STRAIGHT: phase_emoji = "[EW_STRAIGHT 🚦]"; break;
    case PHASE_EW_LEFT:     phase_emoji = "[EW_LEFT 🚦]";     break;
    case PHASE_YELLOW:      phase_emoji = "[YELLOW 🟡]";     break;
    case PHASE_ALL_RED:     phase_emoji = "[ALL_RED 🔴]";     break;
    default:               phase_emoji = "[?????]";          break;
    }

    printf("[t=%04ds] %s phase=%-12s dur=%2ds elap=%2ds | %s\n",
           tick,
           phase_emoji,
           phase_str,
           dec->duration,
           dec->phase_elapsed,
           dec->reason);

    if (verbose) {
        /* 打印各车道队列 */
        printf("         queues: ");
        for (int r = 0; r < ROAD_COUNT; r++) {
            printf("%s:", road_to_string(r));
            for (int l = 0; l < LANE_COUNT; l++) {
                printf("%s=%d", lane_to_string(l), dec->road_lanes[r][l].total);
                if (l < LANE_COUNT - 1) printf(" ");
            }
            if (r < ROAD_COUNT - 1) printf("  ");
        }
        printf("\n");
        printf("         corridor totals: NS=%d EW=%d | mask=0x%03x\n",
               dec->corridor_counts[CORRIDOR_NS],
               dec->corridor_counts[CORRIDOR_EW],
               dec->allowed_lanes_mask);
    }
}

/* 打印当前模拟器队列（调试用） */
static void print_simulator_queues(const Simulator *sim) {
    int queues[4][3];
    simulator_get_lane_queues(sim, queues);
    printf("  [sim queues] N(L=%d S=%d R=%d)  E(L=%d S=%d R=%d)  S(L=%d S=%d R=%d)  W(L=%d S=%d R=%d)\n",
           queues[0][0], queues[0][1], queues[0][2],
           queues[2][0], queues[2][1], queues[2][2],
           queues[1][0], queues[1][1], queues[1][2],
           queues[3][0], queues[3][1], queues[3][2]);
}

/* 主循环: 模拟模式 */
static int run_simulation(const AppConfig *cfg) {
    printf("\n========== Traffic Controller - Simulation Mode v2.0 ==========\n");
    printf("Scenario: %s (%s)\n",
           simulator_scenario_name(cfg->scenario),
           simulator_scenario_desc(cfg->scenario));
    printf("Duration: %d seconds\n", cfg->duration);
    printf("==============================================================\n\n");

    /* 创建模拟器和控制器 */
    SimulatorConfig sim_cfg = {
        .scenario = cfg->scenario,
        .duration_seconds = cfg->duration,
        .tick_interval_ms = 1000
    };
    Simulator *sim = simulator_create(&sim_cfg);
    if (!sim) {
        printf("Error: Failed to create simulator\n");
        return -1;
    }

    /* v2.0:初始相位 = NS_STRAIGHT（代替旧的 PHASE_NS_GREEN）*/
    TrafficController ctrl;
    controller_init(&ctrl, NULL, PHASE_NS_STRAIGHT);

    SimVehicle sim_vehicles[MAX_VEHICLES];
    SignalPhase last_phase = PHASE_NS_STRAIGHT;

    int tick = 0;
    while (g_running && tick < cfg->duration) {
        /* 1. 模拟器生成新到达车辆（加入 sim->lane_queues）*/
        int vehicle_count = simulator_step(sim, sim_vehicles, MAX_VEHICLES);

        /* 2. 把本 tick 到达的车 report 给控制器（更新控制器内部队列）
         *
         * 关键修复：控制器必须用自己队列的状态做决策。
         * 如果直接把 sim_vehicles 传给 controller_step()，控制器看到的是
         * "上 tick 放行后剩余的队列"（已被 simulator_discharge 清空），
         * 会在没有新车到达时误判"队列已空"→立即切灯→变灯过于频繁。
         *
         * 正确流程：
         *   simulator_step() → sim->lane_queues 有了新车
         *   controller_report_vehicle() → ctrl->lane_queues 同步更新
         *   controller_step_internal() → 基于控制器队列做决策（队列不为空）
         *   simulator_discharge() → 模拟器队列放电（视觉同步）
         *   controller_discharge() → 控制器队列放电（状态同步）*/
        for (int i = 0; i < vehicle_count; i++) {
            controller_report_vehicle(&ctrl, sim_vehicles[i].road, sim_vehicles[i].lane);
        }

        /* 3. 控制器决策（基于自己的内部队列）*/
        SignalDecision dec = controller_step_internal(&ctrl);

        /* 4. 放行：同步清空模拟器和控制器的队列
         * simulator_discharge() 控制视觉输出（sim->lane_queues）
         *    controller_discharge() 同步控制器内部状态（ctrl->lane_queues）*/
        int release_rate = ctrl.config.release_rate_per_road;
        switch (dec.phase) {
        case PHASE_NS_STRAIGHT:
        case PHASE_NS_LEFT:
        case PHASE_EW_STRAIGHT:
        case PHASE_EW_LEFT:
            simulator_discharge(sim, dec.phase, release_rate);
            controller_discharge(&ctrl, release_rate);
            break;
        case PHASE_YELLOW:
        case PHASE_ALL_RED:
        default:
            /* 黄灯/全红阶段不放行 */
            break;
        }

        /* 打印状态变化 */
        if (dec.phase != last_phase) {
            printf("\n--- Phase changed: %s -> %s ---\n",
                   phase_to_string(last_phase), phase_to_string(dec.phase));
            print_decision(&dec, tick, cfg->verbose);
            last_phase = dec.phase;
        } else if (cfg->verbose) {
            print_decision(&dec, tick, cfg->verbose);
        }

        if (cfg->verbose) {
            print_simulator_queues(sim);
        }

        /* 更新 LVGL 红绿灯 GUI */
        gui_lvgl_update(&dec, tick);

        tick++;
        /* 等待 1 秒 (边等边伺候 LVGL 渲染) */
        gui_lvgl_sleep_ms(1000);
    }

    simulator_destroy(sim);

    printf("\n========== Simulation Complete ==========\n");
    printf("Ticks: %d | Phase switches: (see above)\n", tick);
    printf("========================================\n");

    return 0;
}

/* 主循环: 真实模式(依赖 MQTT) */
static int run_realtime(const AppConfig *cfg) {
    printf("\n========== Traffic Controller - Real-time Mode ==========\n");
    printf("MQTT Broker: %s:%d\n", cfg->mqtt_host, cfg->mqtt_port);
    printf("RSU ID: %s\n", cfg->rsu_id);
    printf("=========================================================\n\n");

    /* 初始化通信模块 */
    MqttConfig mqtt_cfg = {
        .broker_host = cfg->mqtt_host,
        .broker_port = cfg->mqtt_port,
        .client_id = "openvela_rsu_001",
        .username = "rsu",
        .password = "rsu123",
        .keepalive_sec = 60,
        .use_tls = false
    };

    if (comm_init(&mqtt_cfg) != 0) {
        printf("Error: Failed to initialize comm module\n");
        return -1;
    }

    if (comm_start() != 0) {
        printf("Error: Failed to start MQTT connection\n");
        comm_fini();
        return -1;
    }

    /* 订阅车辆数据 */
    comm_subscribe(MQTT_SUBSCRIBE_TOPIC);

    /* 初始化控制器 */
    TrafficController ctrl;
    controller_init(&ctrl, NULL, PHASE_NS_STRAIGHT);

    char spat_json[256];
    SignalPhase last_phase = PHASE_NS_STRAIGHT;
    int tick = 0;

    while (g_running) {
        /* 检查通信状态 */
        if (!comm_is_connected()) {
            printf("[MAIN] Warning: MQTT disconnected, attempting reconnect...\n");
            comm_start();
            gui_lvgl_sleep_ms(2000);
            continue;
        }

        /* 获取模拟车辆数据(实际应从 MQTT 回调获取) */
        int vehicle_count = 0;
        const SimVehicle *sim_veh = comm_get_sim_vehicles(&vehicle_count);

        /* 转换为控制器输入 (v2.0: road/lane) */
        VehicleState vehicles[MAX_VEHICLES];
        for (int i = 0; i < vehicle_count && i < MAX_VEHICLES; i++) {
            vehicles[i].road = sim_veh[i].road;
            vehicles[i].lane = sim_veh[i].lane;
            vehicles[i].position = sim_veh[i].position;
            vehicles[i].speed = sim_veh[i].speed;
            vehicles[i].is_waiting = sim_veh[i].is_waiting;
            snprintf(vehicles[i].vehicle_id, sizeof(vehicles[i].vehicle_id),
                     "%s", sim_veh[i].vehicle_id);
        }

        /* 控制器决策 */
        SignalDecision dec = controller_step(&ctrl, vehicles, vehicle_count);

        /* 发布 SPaT 消息 */
        const char *phase_str = phase_to_string(dec.phase);
        int remain = dec.duration - dec.phase_elapsed;
        comm_build_spat_json(spat_json, sizeof(spat_json),
                            phase_str, remain, cfg->rsu_id);
        comm_publish(MQTT_PUBLISH_TOPIC, (const uint8_t*)spat_json, strlen(spat_json));

        /* 打印状态变化 */
        if (dec.phase != last_phase) {
            printf("\n--- Phase changed: %s -> %s ---\n",
                   phase_to_string(last_phase), phase_to_string(dec.phase));
            last_phase = dec.phase;
        }

        print_decision(&dec, tick, cfg->verbose);

        /* 报告通信正常 */
        controller_report_comm_ok(&ctrl);

        /* 更新 LVGL 红绿灯 GUI */
        gui_lvgl_update(&dec, tick);

        tick++;
        gui_lvgl_sleep_ms(1000);
    }

    comm_fini();
    return 0;
}

/* 演示模式: 循环播放各场景 */
static int run_demo(void) {
    printf("\n========== Traffic Controller - Demo Mode v2.0 ==========\n");
    printf("Cycling through all scenarios...\n");
    printf("=========================================================\n");

    for (int i = 0; i < SCENARIO_COUNT && g_running; i++) {
        printf("\n\n");
        printf("##################################################\n");
        printf("## Scenario %d/%d: %s\n",
               i + 1, SCENARIO_COUNT, simulator_scenario_name((ScenarioType)i));
        printf("## %s\n", simulator_scenario_desc((ScenarioType)i));
        printf("##################################################\n");

        AppConfig cfg;
        init_config(&cfg);
        cfg.scenario = (ScenarioType)i;
        cfg.duration = 30; /* 每场景30秒 */
        cfg.verbose = true;

        SimulatorConfig sim_cfg = {
            .scenario = cfg.scenario,
            .duration_seconds = cfg.duration,
            .tick_interval_ms = 1000
        };
        Simulator *sim = simulator_create(&sim_cfg);
        TrafficController ctrl;
        controller_init(&ctrl, NULL, PHASE_NS_STRAIGHT);

        SimVehicle sim_vehicles[MAX_VEHICLES];
        SignalPhase last_phase = PHASE_NS_STRAIGHT;

        int tick = 0;
        while (g_running && tick < cfg.duration) {
            /* 1. 模拟器生成车辆 */
            int vehicle_count = simulator_step(sim, sim_vehicles, MAX_VEHICLES);

            /* 2.报告到达给控制器（同步队列）*/
            for (int j = 0; j < vehicle_count; j++) {
                controller_report_vehicle(&ctrl, sim_vehicles[j].road, sim_vehicles[j].lane);
            }

            /* 3. 控制器决策 */
            SignalDecision dec = controller_step_internal(&ctrl);

            /* 4. 放行（同步模拟器和控制器队列）*/
            int release_rate = ctrl.config.release_rate_per_road;
            switch (dec.phase) {
            case PHASE_NS_STRAIGHT:
            case PHASE_NS_LEFT:
            case PHASE_EW_STRAIGHT:
            case PHASE_EW_LEFT:
                simulator_discharge(sim, dec.phase, release_rate);
                controller_discharge(&ctrl, release_rate);
                break;
            case PHASE_YELLOW:
            case PHASE_ALL_RED:
            default:
                break;
            }

            if (dec.phase != last_phase) {
                printf("\n--- %s -> %s ---\n",
                       phase_to_string(last_phase), phase_to_string(dec.phase));
                last_phase = dec.phase;
            }

            print_decision(&dec, tick, true);
            print_simulator_queues(sim);

            /* 更新 LVGL 红绿灯 GUI */
            gui_lvgl_update(&dec, tick);

            tick++;
            gui_lvgl_sleep_ms(1000);
        }

        simulator_destroy(sim);

        if (!g_running) break;
    }

    printf("\n========== Demo Complete ==========\n");
    return 0;
}

/* 主函数入口 */
int main(int argc, FAR char *argv[]) {
    AppConfig cfg;

    /* 注册信号处理 */
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

#ifdef NEED_BOARDINIT
    /* 初始化板级驱动 (LCD 等), 参照 lvgldemo */
    boardctl(BOARDIOC_INIT, 0);
#endif

    /* 解析参数 */
    if (parse_args(&cfg, argc, argv) != 0) {
        print_help(argv[0]);
        return 0;
    }

    /* 打印启动信息 */
    printf("\n");
    printf("╔═══════════════════════════════════════════════════════╗\n");
    printf("║   OpenVela Smart Traffic Controller v%s            ║\n", VERSION);
    printf("║   Role B: Signal Decision & Safety Management      ║\n");
    printf("║   v2.0: 4-phase independent (NS_STRAIGHT/LEFT, ║\n");
    printf("║         EW_STRAIGHT/LEFT + YELLOW + ALL_RED)       ║\n");
    printf("╚═══════════════════════════════════════════════════════╝\n");
    printf("\n");

    /* 初始化 LVGL 红绿灯可视化 GUI */
    gui_lvgl_init();
    /* 给 LVGL 后台渲染任务充足启动时间 (lvgldemo 跑好几秒, 100ms 不够) */
    gui_lvgl_sleep_ms(500);

    int ret;
    if (cfg.demo_mode) {
        ret = run_demo();
    } else if (cfg.sim_mode) {
        ret = run_simulation(&cfg);
    } else {
        /* 默认使用模拟模式（不依赖 MQTT）*/
        ret = run_simulation(&cfg);
    }

    printf("\n[MAIN] Exiting with code %d\n", ret);
    return ret;
}