/**
 * @file simulator.c
 * @brief 三车道交通模拟器实现
 *
 * 离散时间步模拟器，按固定规则生成车辆到达并维护车道队列。
 *
 * ## 右转始终通行
 *
 * 右转车辆在最右侧车道，理论上任何时候都可以通行。
 * simulator_discharge() 按信号相位放行车辆，但实际通行判断应使用
 * controller_can_pass()，右转会始终返回 true。
 *
 * @author OpenVela Traffic Team
 * @version 1.1.0
 */

#include "simulator.h"
#include "signal_controller.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* ========================================================================
 * 静态数据
 * ======================================================================== */

/** @brief 场景名称表 */
static const char* scenario_names[SCENARIO_COUNT] = {
    [SCENARIO_BALANCED]        = "balanced",
    [SCENARIO_NS_RUSH]         = "ns_rush",
    [SCENARIO_EW_RUSH]         = "ew_rush",
    [SCENARIO_LEFT_RUSH]       = "left_rush",
    [SCENARIO_ALTERNATING_WAVE] = "alternating_wave",
    [SCENARIO_IDLE]            = "idle"
};

/** @brief 场景描述表 */
static const char* scenario_descs[SCENARIO_COUNT] = {
    [SCENARIO_BALANCED]        = "南北/东西流量均衡，三车道均匀分布",
    [SCENARIO_NS_RUSH]         = "南北方向高峰期，左转需求较大",
    [SCENARIO_EW_RUSH]         = "东西方向高峰期，左转需求较大",
    [SCENARIO_LEFT_RUSH]       = "各方向左转需求明显",
    [SCENARIO_ALTERNATING_WAVE] = "前30秒NS高峰，后30秒EW高峰",
    [SCENARIO_IDLE]            = "无车辆输入（空载测试）"
};

/* ========================================================================
 * 工具函数
 * ======================================================================== */

/**
 * @brief 获取场景名称
 */
const char* simulator_scenario_name(ScenarioType type) {
    if (type < 0 || type >= SCENARIO_COUNT) return "unknown";
    return scenario_names[type];
}

/**
 * @brief 获取场景描述
 */
const char* simulator_scenario_desc(ScenarioType type) {
    if (type < 0 || type >= SCENARIO_COUNT) return "?";
    return scenario_descs[type];
}

/* ========================================================================
 * 模拟器生命周期
 * ======================================================================== */

/**
 * @brief 创建模拟器
 */
Simulator* simulator_create(const SimulatorConfig *config) {
    Simulator *sim = (Simulator*)malloc(sizeof(Simulator));
    if (!sim) return NULL;

    memset(sim, 0, sizeof(Simulator));
    if (config) {
        memcpy(&sim->config, config, sizeof(SimulatorConfig));
    } else {
        sim->config.scenario = SCENARIO_BALANCED;
        sim->config.duration_seconds = 120;
        sim->config.tick_interval_ms = 1000;
    }

    return sim;
}

/**
 * @brief 销毁模拟器
 */
void simulator_destroy(Simulator *sim) {
    if (sim) free(sim);
}

/**
 * @brief 重置模拟器
 */
void simulator_reset(Simulator *sim) {
    if (!sim) return;
    memset(sim->lane_queues, 0, sizeof(sim->lane_queues));
    sim->current_tick = 0;
    sim->vehicle_sequence = 0;
    sim->running = false;
}

/* ========================================================================
 * 车辆到达生成
 * ======================================================================== */

/**
 * @brief 计算每秒各方向各车道到达车辆数
 *
 * 各场景的到达规则：
 *
 * **balanced**: 每3秒南北/东西各来一辆直行，每6秒各方向来一辆左转
 *
 * **ns_rush**: 每2秒南北密集到达（直行+右转），每4秒来左转；东西稀疏
 *
 * **ew_rush**: 东西高峰，南北稀疏
 *
 * **left_rush**: 每2秒各方向交替来左转车，每5秒来一辆直行
 *
 * **alternating_wave**: 前30秒NS高峰，后30秒EW高峰，之后均衡
 *
 * **idle**: 无车辆到达
 */
static void arrivals_for_second(Simulator *sim, int second,
                                 int arrivals_out[4][3]) {
    memset(arrivals_out, 0, sizeof(int) * 4 * 3);

    switch (sim->config.scenario) {
    case SCENARIO_BALANCED:
        if (second % 3 == 0) {
            arrivals_out[0][1] = 1;  /* N 直行 */
        }
        if (second % 3 == 1) {
            arrivals_out[1][1] = 1;  /* S 直行 */
        }
        if (second % 3 == 0) {
            arrivals_out[2][1] = 1;  /* E 直行 */
        }
        if (second % 3 == 1) {
            arrivals_out[3][1] = 1;  /* W 直行 */
        }
        if (second % 6 == 2) {
            arrivals_out[0][0] = 1;  /* N 左转 */
            arrivals_out[1][0] = 1;  /* S 左转 */
        }
        if (second % 6 == 3) {
            arrivals_out[2][0] = 1;  /* E 左转 */
            arrivals_out[3][0] = 1;  /* W 左转 */
        }
        break;

    case SCENARIO_NS_RUSH:
        if (second % 2 == 0) {
            arrivals_out[0][1] = 1;  /* N 直行 */
            arrivals_out[0][2] = 1;  /* N 右转 */
            arrivals_out[1][1] = 1;  /* S 直行 */
            arrivals_out[1][2] = 1;  /* S 右转 */
        }
        if (second % 4 == 2) {
            arrivals_out[0][0] = 1;  /* N 左转 */
            arrivals_out[1][0] = 1;  /* S 左转 */
        }
        if (second % 8 == 0) {
            arrivals_out[2][1] = 1;  /* E 直行 */
        }
        break;

    case SCENARIO_EW_RUSH:
        if (second % 2 == 0) {
            arrivals_out[2][1] = 1;  /* E 直行 */
            arrivals_out[2][2] = 1;  /* E 右转 */
            arrivals_out[3][1] = 1;  /* W 直行 */
            arrivals_out[3][2] = 1;  /* W 右转 */
        }
        if (second % 4 == 2) {
            arrivals_out[2][0] = 1;  /* E 左转 */
            arrivals_out[3][0] = 1;  /* W 左转 */
        }
        break;

    case SCENARIO_LEFT_RUSH:
        if (second % 2 == 0) {
            arrivals_out[0][0] = 1;  /* N 左转 */
            arrivals_out[1][0] = 1;  /* S 左转 */
        }
        if (second % 2 == 1) {
            arrivals_out[2][0] = 1;  /* E 左转 */
            arrivals_out[3][0] = 1;  /* W 左转 */
        }
        if (second % 5 == 2) {
            arrivals_out[0][1] = 1;  /* N 直行 */
        }
        if (second % 5 == 3) {
            arrivals_out[2][1] = 1;  /* E 直行 */
        }
        break;

    case SCENARIO_ALTERNATING_WAVE:
        if (second < 30) {
            arrivals_out[0][1] = 2;  /* N 直行（密集）*/
            arrivals_out[1][1] = 2;  /* S 直行 */
            arrivals_out[0][0] = 1;  /* N 左转 */
            arrivals_out[1][0] = 1;  /* S 左转 */
        } else if (second < 60) {
            arrivals_out[2][1] = 2;  /* E 直行 */
            arrivals_out[3][1] = 2;  /* W 直行 */
            arrivals_out[2][0] = 1;  /* E 左转 */
            arrivals_out[3][0] = 1;  /* W 左转 */
        } else {
            arrivals_out[0][1] = (second % 4 == 0) ? 1 : 0;
            arrivals_out[1][1] = (second % 4 == 2) ? 1 : 0;
            arrivals_out[2][1] = (second % 5 == 0) ? 1 : 0;
            arrivals_out[3][1] = (second % 5 == 2) ? 1 : 0;
        }
        break;

    case SCENARIO_IDLE:
    default:
        /* 无到达 */
        break;
    }
}

/* ========================================================================
 * 车辆状态构建
 * ======================================================================== */

/**
 * @brief 构建 VehicleState 数组
 *
 * 遍历所有车道队列，生成 SimVehicle 列表。
 * 车辆ID格式：方向+车道首字母+序号，如 "N-L-123"
 * 位置按队列顺序递增排列。
 */
static int build_vehicle_states(Simulator *sim, SimVehicle *out, int max_count) {
    int idx = 0;

    for (int road = 0; road < 4 && idx < max_count; road++) {
        for (int lane = 0; lane < 3 && idx < max_count; lane++) {
            int queue_size = sim->lane_queues[road][lane];

            for (int i = 0; i < queue_size && idx < max_count; i++) {
                sim->vehicle_sequence++;

                const char* road_names[] = {"N", "S", "E", "W"};
                const char* lane_names[] = {"L", "S", "R"};
                snprintf(out[idx].vehicle_id, sizeof(out[idx].vehicle_id),
                         "%s%s-%d",
                         road_names[road],
                         lane_names[lane],
                         sim->vehicle_sequence);

                out[idx].road = road;
                out[idx].lane = lane;
                out[idx].position = 5.0f + (float)(i * 5);
                out[idx].speed = 0.0f;
                out[idx].is_waiting = true;

                idx++;
            }
        }
    }

    return idx;
}

/* ========================================================================
 * 放行逻辑
 * ======================================================================== */

/**
 * @brief 按相位放行车辆
 *
 * 4 个主相位 + YELLOW/ALL_RED：
 *
 * - NS_STRAIGHT: N/S 的直行+右转车道放行
 * - NS_LEFT:     N/S 的左转车道放行
 * - EW_STRAIGHT: E/W 的直行+右转车道放行
 * - EW_LEFT:     E/W 的左转车道放行
 * - YELLOW/ALL_RED: 不放行
 *
 * @note 右转车辆在 YELLOW/ALL_RED 阶段实际仍可通行，
 *       此处仅用于模拟队列变化。通行判断见 controller_can_pass()。
 */
void simulator_discharge(Simulator *sim, int phase, int release_rate) {
    switch (phase) {
    case PHASE_NS_STRAIGHT:
        for (int road = 0; road <= 1; road++) {
            for (int lane = 1; lane <= 2; lane++) {  /* STRAIGHT + RIGHT */
                int n = (sim->lane_queues[road][lane] > release_rate) ?
                        release_rate : sim->lane_queues[road][lane];
                sim->lane_queues[road][lane] -= n;
            }
        }
        break;

    case PHASE_NS_LEFT:
        for (int road = 0; road <= 1; road++) {
            int n = (sim->lane_queues[road][0] > release_rate) ?
                    release_rate : sim->lane_queues[road][0];
            sim->lane_queues[road][0] -= n;
        }
        break;

    case PHASE_EW_STRAIGHT:
        for (int road = 2; road <= 3; road++) {
            for (int lane = 1; lane <= 2; lane++) {
                int n = (sim->lane_queues[road][lane] > release_rate) ?
                        release_rate : sim->lane_queues[road][lane];
                sim->lane_queues[road][lane] -= n;
            }
        }
        break;

    case PHASE_EW_LEFT:
        for (int road = 2; road <= 3; road++) {
            int n = (sim->lane_queues[road][0] > release_rate) ?
                    release_rate : sim->lane_queues[road][0];
            sim->lane_queues[road][0] -= n;
        }
        break;

    default:
        /* YELLOW / ALL_RED: 不放行 */
        break;
    }
}

/* ========================================================================
 * 队列查询
 * ======================================================================== */

/**
 * @brief 获取当前各车道队列长度
 */
void simulator_get_lane_queues(const Simulator *sim, int queues_out[4][3]) {
    if (!sim || !queues_out) return;
    memcpy(queues_out, sim->lane_queues, sizeof(sim->lane_queues));
}

/* ========================================================================
 * 主模拟循环
 * ======================================================================== */

/**
 * @brief 执行一步模拟
 */
int simulator_step(Simulator *sim, SimVehicle *vehicles_out, int max_count) {
    if (!sim) return 0;

    int second = sim->current_tick;

    /* 1. 生成新到达车辆 */
    int arrivals[4][3];
    arrivals_for_second(sim, second, arrivals);
    for (int r = 0; r < 4; r++) {
        for (int l = 0; l < 3; l++) {
            sim->lane_queues[r][l] += arrivals[r][l];
        }
    }

    /* 2. 构建车辆列表 */
    int count = 0;
    if (vehicles_out && max_count > 0) {
        count = build_vehicle_states(sim, vehicles_out, max_count);
    }

    /* 3. 更新状态 */
    sim->current_tick++;
    sim->running = (second < sim->config.duration_seconds);

    return count;
}