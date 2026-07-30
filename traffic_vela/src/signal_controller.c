/**
 * @file signal_controller.c
 * @brief 三车道版智能信号控制器 - 4 相位独立控制
 *
 * 状态机设计：
 * 1. 安全模式：通信中断时锁定 NS_STRAIGHT，不切换
 * 2. 正常模式：4 个相位之间通过经典 NEMA 双环规则切换
 *    NS_STRAIGHT ↔ NS_LEFT (环 A)
 *    EW_STRAIGHT ↔ EW_LEFT (环 B)
 *    环间允许交错（NS 在 EW 之前 / EW 在 NS 之前）
 *
 * ## 切换规则（按优先级）
 *
 * A. 强制轮转：达到 max_green 必须切到 YELLOW
 * B. 智能早切：当前方向清空 + 对向/本向其他相位有需求
 * C. 防饥饿：硬阈值 30s 没被服务 → 切
 * D. LEFT_ONLY 阶段：所有左转清空后切换
 *
 * @author OpenVela Traffic Team
 * @version 2.0.0
 */

#include "signal_controller.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* ========================================================================
 * 静态数据
 * ======================================================================== */

static const int road_to_corridor[ROAD_COUNT] = {
    [ROAD_N] = CORRIDOR_NS,
    [ROAD_S] = CORRIDOR_NS,
    [ROAD_E] = CORRIDOR_EW,
    [ROAD_W] = CORRIDOR_EW
};

/** 相位名称 */
static const char* phase_names[PHASE_COUNT] = {
    [PHASE_NS_STRAIGHT] = "NS_STRAIGHT",
    [PHASE_NS_LEFT]     = "NS_LEFT",
    [PHASE_EW_STRAIGHT] = "EW_STRAIGHT",
    [PHASE_EW_LEFT]     = "EW_LEFT",
    [PHASE_YELLOW]      = "YELLOW",
    [PHASE_ALL_RED]     = "ALL_RED"
};

static const char* road_names[ROAD_COUNT]   = { "N", "S", "E", "W" };
static const char* lane_names[LANE_COUNT]   = { "LEFT", "STRAIGHT", "RIGHT" };
static const char* corridor_names[CORRIDOR_COUNT] = { "NS", "EW" };

/* ========================================================================
 * 工具
 * ======================================================================== */

static int opposite_corridor(int corridor) {
    return (corridor == CORRIDOR_NS) ? CORRIDOR_EW : CORRIDOR_NS;
}

/* ========================================================================
 * 通行许可
 * ======================================================================== */

/**
 * @brief 获取指定相位的 12-bit 通行许可
 */
int controller_get_allowed_lanes(const TrafficController *ctrl, SignalPhase phase) {
    (void)ctrl;
    switch (phase) {
    case PHASE_NS_STRAIGHT:
        /* N/S 直行+右转 */
        return PERMIT_N_STRAIGHT | PERMIT_N_RIGHT
             | PERMIT_S_STRAIGHT | PERMIT_S_RIGHT;
    case PHASE_NS_LEFT:
        /* N/S 左转 */
        return PERMIT_N_LEFT | PERMIT_S_LEFT;
    case PHASE_EW_STRAIGHT:
        /* E/W 直行+右转 */
        return PERMIT_E_STRAIGHT | PERMIT_E_RIGHT
             | PERMIT_W_STRAIGHT | PERMIT_W_RIGHT;
    case PHASE_EW_LEFT:
        /* E/W 左转 */
        return PERMIT_E_LEFT | PERMIT_W_LEFT;
    case PHASE_YELLOW:
    case PHASE_ALL_RED:
        /* 全停，但允许右转（实际中可继续） */
        return RIGHT_MASK_ALL;
    default:
        return 0;
    }
}

bool controller_can_pass(const TrafficController *ctrl,
                        const VehicleState *vehicle,
                        SignalDecision *decision) {
    if (!ctrl || !vehicle || !decision) return false;

    int bit = vehicle->road * 3 + vehicle->lane;
    if (bit < 0 || bit > 11) return false;
    return (decision->allowed_lanes_mask & (1 << bit)) != 0;
}

/* ========================================================================
 * 配置
 * ======================================================================== */

void controller_get_default_config(ControllerConfig *config) {
    if (!config) return;
    memset(config, 0, sizeof(ControllerConfig));

    /* 板子手推演示调优：基础时长较长，让观众看清状态变化
     * 饥饿阈值在 decide_next_phase 内部硬编码（30 ticks） */
    config->base_green              = 20;  /* 基础绿灯 20s（演示友好） */
    config->min_green               = 10;  /* 最小绿灯 10s（用户要求：绿灯至少10秒） */
    config->max_green               = 60;  /* 最大绿灯 60s（演示不会切太快） */
    config->yellow_time             = 4;   /* 黄灯 4s（演示看得清） */
    config->all_red_time            = 2;   /* 全红 2s（清空路口需要足够时间） */
    config->left_green_min          = 10;  /* 左转最小 10s（与主绿灯同步） */
    config->left_green_max          = 25;  /* 左转最大 25s */
    config->release_rate_per_road   = 4;
    config->left_green_max          = 20;
    config->release_rate_per_road   = 4;

    config->vehicle_weight          = 1.0f;
    config->waiting_bonus           = 1.2f;
    config->stopped_queue_bonus     = 0.8f;
    config->proximity_bonus         = 1.5f;
    config->proximity_range         = 60.0f;
    config->starvation_weight       = 0.45f;
    config->green_adjustment_window = 15;
}

void controller_init(TrafficController *ctrl,
                    const ControllerConfig *config,
                    SignalPhase initial_phase) {
    if (!ctrl) return;

    if (config) {
        memcpy(&ctrl->config, config, sizeof(ControllerConfig));
    } else {
        controller_get_default_config(&ctrl->config);
    }

    /* 验证初始相位：只接受 4 个主绿灯相位之一 */
    if (initial_phase != PHASE_NS_STRAIGHT && initial_phase != PHASE_NS_LEFT &&
        initial_phase != PHASE_EW_STRAIGHT && initial_phase != PHASE_EW_LEFT) {
        initial_phase = PHASE_NS_STRAIGHT;
    }

    ctrl->current_phase           = initial_phase;
    ctrl->current_phase_elapsed   = 0;
    ctrl->current_stage_duration  = ctrl->config.base_green;
    ctrl->pending_green_phase     = initial_phase;
    ctrl->waiting_seconds[CORRIDOR_NS] = 0;
    ctrl->waiting_seconds[CORRIDOR_EW] = 0;
    strncpy(ctrl->last_reason, "initial startup", sizeof(ctrl->last_reason) - 1);
    ctrl->last_reason[sizeof(ctrl->last_reason) - 1] = '\0';
    ctrl->emergency_mode    = EMERGENCY_NONE;
    ctrl->comm_lost_ticks   = 0;
    ctrl->max_comm_lost_ticks = 5;
    ctrl->tick_count        = 0;
    ctrl->switch_count      = 0;
    memset(ctrl->lane_queues, 0, sizeof(ctrl->lane_queues));

    /* 初始化饥饿计数器：当前正在服务的相位 = 0，其他 = 0 */
    ctrl->ticks_since_ns_straight = 0;
    ctrl->ticks_since_ns_left     = 0;
    ctrl->ticks_since_ew_straight = 0;
    ctrl->ticks_since_ew_left     = 0;
    ctrl->safe_mode = 0;
}

void controller_reset(TrafficController *ctrl) {
    if (!ctrl) return;
    controller_init(ctrl, &ctrl->config, PHASE_NS_STRAIGHT);
}

/* ========================================================================
 * 评分
 * ======================================================================== */

static float score_vehicle(const VehicleState *v, const ControllerConfig *cfg) {
    float score = cfg->vehicle_weight;
    if (v->is_waiting) {
        score += cfg->waiting_bonus;
        if (v->speed == 0.0f) score += cfg->stopped_queue_bonus;
    }
    float capped = (v->position < cfg->proximity_range) ? v->position : cfg->proximity_range;
    float pf = 1.0f - (capped / cfg->proximity_range);
    if (pf > 0.0f) score += pf * cfg->proximity_bonus;
    return score;
}

typedef struct {
    LaneStats road_lanes[ROAD_COUNT][LANE_COUNT];
    int corridor_counts[CORRIDOR_COUNT];
    float raw_scores[CORRIDOR_COUNT];
    float effective_scores[CORRIDOR_COUNT];

    /* 4 个独立相位的需求评分 */
    float score_ns_straight;
    float score_ns_left;
    float score_ew_straight;
    float score_ew_left;
} DemandSnapshot;

static void compute_snapshot(const VehicleState *vehicles, int count,
                            const int waiting_seconds[CORRIDOR_COUNT],
                            const ControllerConfig *cfg,
                            DemandSnapshot *snap) {
    memset(snap, 0, sizeof(DemandSnapshot));

    for (int i = 0; i < count; i++) {
        const VehicleState *v = &vehicles[i];
        if (v->road < 0 || v->road >= ROAD_COUNT) continue;
        if (v->lane < 0 || v->lane >= LANE_COUNT) continue;

        int corridor = road_to_corridor[v->road];
        snap->road_lanes[v->road][v->lane].total++;
        if (v->is_waiting) snap->road_lanes[v->road][v->lane].waiting++;

        float sc = score_vehicle(v, cfg);
        snap->raw_scores[corridor] += sc;

        /* 按车道类型分桶到 4 个相位
         *
         * 直行和右转都归入"直行相位"（因为 NS_STRAIGHT 包含右转）。
         * 这样：1 辆右转车到达后，控制器会识别到有 NS/EW 直行需求，
         * 调度器会安排 NS_STRAIGHT 或 EW_STRAIGHT 相位。
         *
         * 虽然右转在 YELLOW 阶段就能通行（不阻塞），
         * 但纳入评分可以让调度器在必要时主动切换相位。
         */
        if (corridor == CORRIDOR_NS) {
            if (v->lane == LANE_LEFT) {
                snap->score_ns_left += sc;
            } else {
                /* 直行+右转都进 NS_STRAIGHT 评分 */
                snap->score_ns_straight += sc;
            }
        } else {
            if (v->lane == LANE_LEFT) {
                snap->score_ew_left += sc;
            } else {
                snap->score_ew_straight += sc;
            }
        }
    }

    for (int c = 0; c < CORRIDOR_COUNT; c++) {
        if (snap->raw_scores[c] <= 0.0f) {
            snap->effective_scores[c] = 0.0f;
        } else {
            snap->effective_scores[c] = snap->raw_scores[c] +
                (float)waiting_seconds[c] * cfg->starvation_weight;
        }
    }
}

/* ========================================================================
 * 绿灯时长规划
 * ======================================================================== */

static int plan_green_duration(SignalPhase phase, const DemandSnapshot *snap,
                               const ControllerConfig *cfg) {
    /* 取出本相位评分和对向走廊评分 */
    float my = 0, opp = 0;
    switch (phase) {
    case PHASE_NS_STRAIGHT:
        my = snap->score_ns_straight;
        opp = snap->score_ew_straight;
        break;
    case PHASE_NS_LEFT:
        my = snap->score_ns_left;
        opp = snap->score_ew_left;  /* 左转对向是另一走廊的左转 */
        break;
    case PHASE_EW_STRAIGHT:
        my = snap->score_ew_straight;
        opp = snap->score_ns_straight;
        break;
    case PHASE_EW_LEFT:
        my = snap->score_ew_left;
        opp = snap->score_ns_left;
        break;
    default:
        return cfg->base_green;
    }

    float total = my + opp;
    if (total <= 0.0f) return cfg->base_green;

    float delta = (my - opp) / total;
    int delta_sec = (int)(delta * (float)cfg->green_adjustment_window + 0.5f);
    int planned = cfg->base_green + delta_sec;

    if (planned < cfg->min_green) planned = cfg->min_green;
    if (planned > cfg->max_green) planned = cfg->max_green;

    /* 左转时长特殊处理 */
    if (phase == PHASE_NS_LEFT || phase == PHASE_EW_LEFT) {
        if (planned < cfg->left_green_min) planned = cfg->left_green_min;
        if (planned > cfg->left_green_max) planned = cfg->left_green_max;
    }

    return planned;
}

/* ========================================================================
 * 切换决策
 *
 * 4 相位状态机：
 * - 直行相位（NS_STRAIGHT, EW_STRAIGHT）：本走廊所有直行+右转都清空
 * - 左转相位（NS_LEFT, EW_LEFT）：本走廊所有左转都清空
 * ======================================================================== */

typedef struct {
    bool should_switch;
    SignalPhase target_phase;
    char reason[128];
} SwitchDecision;

/* 检查某走廊"直行相位"（直行+右转）是否完全清空
 *
 * 因为 NS_STRAIGHT/EW_STRAIGHT 相位同时放行直行和右转，
 * 所以"直行相位清空"应该同时检查这两种车道。
 */
static int corridor_straight_count(const DemandSnapshot *snap, int corridor) {
    int n = 0;
    int r_start = (corridor == CORRIDOR_NS) ? ROAD_N : ROAD_E;
    int r_end   = (corridor == CORRIDOR_NS) ? ROAD_S : ROAD_W;
    for (int r = r_start; r <= r_end; r++) {
        n += snap->road_lanes[r][LANE_STRAIGHT].total;
        n += snap->road_lanes[r][LANE_RIGHT].total;
    }
    return n;
}

/* 检查某走廊左转是否完全清空 */
static int corridor_left_count(const DemandSnapshot *snap, int corridor) {
    int n = 0;
    int r_start = (corridor == CORRIDOR_NS) ? ROAD_N : ROAD_E;
    int r_end   = (corridor == CORRIDOR_NS) ? ROAD_S : ROAD_W;
    for (int r = r_start; r <= r_end; r++) {
        n += snap->road_lanes[r][LANE_LEFT].total;
    }
    return n;
}

static void decide_next_phase(const TrafficController *ctrl,
                               const DemandSnapshot *snap,
                               SwitchDecision *dec) {
    dec->should_switch = false;
    dec->reason[0] = '\0';

    SignalPhase current = ctrl->current_phase;
    const ControllerConfig *cfg = &ctrl->config;
    int elapsed = ctrl->current_phase_elapsed;

    /* 只在 4 个主绿灯相位才决策 */
    if (current != PHASE_NS_STRAIGHT && current != PHASE_NS_LEFT &&
        current != PHASE_EW_STRAIGHT && current != PHASE_EW_LEFT) {
        return;
    }

    /* === A. 强制轮转：达到最大绿灯 === */
    if (elapsed >= cfg->max_green) {
        dec->should_switch = true;
        /* 默认切到对向走廊的对应相位（NS_STRAIGHT → EW_STRAIGHT） */
        dec->target_phase = (current == PHASE_NS_STRAIGHT) ? PHASE_EW_STRAIGHT :
                            (current == PHASE_NS_LEFT)     ? PHASE_EW_LEFT     :
                            (current == PHASE_EW_STRAIGHT) ? PHASE_NS_STRAIGHT :
                                                             PHASE_NS_LEFT;
        snprintf(dec->reason, sizeof(dec->reason),
                 "max green (%ds) reached", cfg->max_green);
        return;
    }

    /* === C. 防饥饿：硬阈值 30s === */
    if (current != PHASE_NS_STRAIGHT &&
        ctrl->ticks_since_ns_straight >= 30 &&
        snap->score_ns_straight > 0) {
        dec->should_switch = true;
        dec->target_phase = PHASE_NS_STRAIGHT;
        snprintf(dec->reason, sizeof(dec->reason),
                 "NS straight hard starvation (%d ticks)",
                 ctrl->ticks_since_ns_straight);
        return;
    }
    if (current != PHASE_NS_LEFT &&
        ctrl->ticks_since_ns_left >= 30 &&
        snap->score_ns_left > 0) {
        dec->should_switch = true;
        dec->target_phase = PHASE_NS_LEFT;
        snprintf(dec->reason, sizeof(dec->reason),
                 "NS left hard starvation (%d ticks)",
                 ctrl->ticks_since_ns_left);
        return;
    }
    if (current != PHASE_EW_STRAIGHT &&
        ctrl->ticks_since_ew_straight >= 30 &&
        snap->score_ew_straight > 0) {
        dec->should_switch = true;
        dec->target_phase = PHASE_EW_STRAIGHT;
        snprintf(dec->reason, sizeof(dec->reason),
                 "EW straight hard starvation (%d ticks)",
                 ctrl->ticks_since_ew_straight);
        return;
    }
    if (current != PHASE_EW_LEFT &&
        ctrl->ticks_since_ew_left >= 30 &&
        snap->score_ew_left > 0) {
        dec->should_switch = true;
        dec->target_phase = PHASE_EW_LEFT;
        snprintf(dec->reason, sizeof(dec->reason),
                 "EW left hard starvation (%d ticks)",
                 ctrl->ticks_since_ew_left);
        return;
    }

    /* === B. 智能早切：当前方向清空 + 至少已保持 min_green 秒 ===
     *
     * 防止在稀疏到达场景下过度切换：
     * - release_rate 较大时，队列可能在 1 tick 内清空
     * - 下一 tick 就有新车到达，过早切走会浪费绿灯
     * - 必须等 min_green 秒才允许 early-switch */
    if (elapsed < cfg->min_green) {
        /* 未达到最小保持时间，禁止 early-switch */
    } else if (current == PHASE_NS_STRAIGHT) {
        if (corridor_straight_count(snap, CORRIDOR_NS) == 0) {
            if (corridor_left_count(snap, CORRIDOR_NS) > 0) {
                dec->should_switch = true;
                dec->target_phase = PHASE_NS_LEFT;
                snprintf(dec->reason, sizeof(dec->reason),
                         "NS straight cleared (%ds), NS left pending",
                         elapsed);
                return;
            }
            if (snap->score_ew_straight > 0 || snap->score_ew_left > 0) {
                dec->should_switch = true;
                dec->target_phase = PHASE_EW_STRAIGHT;
                snprintf(dec->reason, sizeof(dec->reason),
                         "NS straight cleared (%ds), switch to EW",
                         elapsed);
                return;
            }
        }
    } else if (current == PHASE_NS_LEFT) {
        if (corridor_left_count(snap, CORRIDOR_NS) == 0) {
            if (snap->score_ew_straight > 0 || snap->score_ew_left > 0) {
                dec->should_switch = true;
                dec->target_phase = PHASE_EW_STRAIGHT;
                snprintf(dec->reason, sizeof(dec->reason),
                         "NS left cleared (%ds), switch to EW",
                         elapsed);
                return;
            }
        }
    } else if (current == PHASE_EW_STRAIGHT) {
        if (corridor_straight_count(snap, CORRIDOR_EW) == 0) {
            if (corridor_left_count(snap, CORRIDOR_EW) > 0) {
                dec->should_switch = true;
                dec->target_phase = PHASE_EW_LEFT;
                snprintf(dec->reason, sizeof(dec->reason),
                         "EW straight cleared (%ds), EW left pending",
                         elapsed);
                return;
            }
            if (snap->score_ns_straight > 0 || snap->score_ns_left > 0) {
                dec->should_switch = true;
                dec->target_phase = PHASE_NS_STRAIGHT;
                snprintf(dec->reason, sizeof(dec->reason),
                         "EW straight cleared (%ds), switch to NS",
                         elapsed);
                return;
            }
        }
    } else if (current == PHASE_EW_LEFT) {
        if (corridor_left_count(snap, CORRIDOR_EW) == 0) {
            if (snap->score_ns_straight > 0 || snap->score_ns_left > 0) {
                dec->should_switch = true;
                dec->target_phase = PHASE_NS_STRAIGHT;
                snprintf(dec->reason, sizeof(dec->reason),
                         "EW left cleared (%ds), switch to NS",
                         elapsed);
                return;
            }
        }
    }
}

/* ========================================================================
 * 主控制逻辑
 * ======================================================================== */

SignalDecision controller_step(TrafficController *ctrl,
                               const VehicleState *vehicles,
                               int vehicle_count) {
    SignalDecision dec = {0};
    if (!ctrl) return dec;

    ctrl->tick_count++;

    /* === 安全模式 === */
    if (ctrl->emergency_mode == EMERGENCY_COMM_LOST ||
        ctrl->comm_lost_ticks >= ctrl->max_comm_lost_ticks) {
        ctrl->emergency_mode = EMERGENCY_COMM_LOST;
        ctrl->safe_mode = 1;

        /* 锁定 NS_STRAIGHT 相位 */
        if (ctrl->current_phase != PHASE_NS_STRAIGHT) {
            ctrl->current_phase = PHASE_YELLOW;
            ctrl->current_phase_elapsed = 0;
            ctrl->pending_green_phase = PHASE_NS_STRAIGHT;
            strncpy(ctrl->last_reason, "safe mode: locking to NS straight",
                    sizeof(ctrl->last_reason) - 1);
        } else {
            ctrl->current_phase_elapsed = 0;
            ctrl->current_stage_duration = ctrl->config.base_green;
            strncpy(ctrl->last_reason, "safe mode: locked to NS straight",
                    sizeof(ctrl->last_reason) - 1);
        }
        ctrl->ticks_since_ns_straight = 0;
        ctrl->ticks_since_ns_left = 0;
        ctrl->ticks_since_ew_straight = 0;
        ctrl->ticks_since_ew_left = 0;
    } else if (ctrl->safe_mode && ctrl->comm_lost_ticks == 0) {
        /* 通信恢复，退出安全模式 */
        ctrl->safe_mode = 0;
        strncpy(ctrl->last_reason, "safe mode exit: comm restored",
                sizeof(ctrl->last_reason) - 1);
    }

    /* 计算需求快照 */
    DemandSnapshot snap;
    compute_snapshot(vehicles, vehicle_count,
                    ctrl->waiting_seconds, &ctrl->config, &snap);

    SignalPhase phase_for_tick = ctrl->current_phase;
    int stage_duration;
    const char *reason = ctrl->last_reason;

    if (phase_for_tick == PHASE_YELLOW) {
        stage_duration = ctrl->config.yellow_time;
    } else if (phase_for_tick == PHASE_ALL_RED) {
        stage_duration = ctrl->config.all_red_time;
    } else {
        stage_duration = ctrl->current_stage_duration;
    }

    /* === 更新饥饿计数器（每个 tick 一次）=== */
    if (phase_for_tick == PHASE_NS_STRAIGHT) {
        ctrl->ticks_since_ns_straight = 0;
    } else {
        ctrl->ticks_since_ns_straight++;
    }
    if (phase_for_tick == PHASE_NS_LEFT) {
        ctrl->ticks_since_ns_left = 0;
    } else {
        ctrl->ticks_since_ns_left++;
    }
    if (phase_for_tick == PHASE_EW_STRAIGHT) {
        ctrl->ticks_since_ew_straight = 0;
    } else {
        ctrl->ticks_since_ew_straight++;
    }
    if (phase_for_tick == PHASE_EW_LEFT) {
        ctrl->ticks_since_ew_left = 0;
    } else {
        ctrl->ticks_since_ew_left++;
    }

    /* === 4 个主绿灯相位的处理 === */
    if (phase_for_tick == PHASE_NS_STRAIGHT ||
        phase_for_tick == PHASE_NS_LEFT ||
        phase_for_tick == PHASE_EW_STRAIGHT ||
        phase_for_tick == PHASE_EW_LEFT) {

        /* 更新走廊等待计时 */
        int active_corridor = -1;
        if (phase_for_tick == PHASE_NS_STRAIGHT || phase_for_tick == PHASE_NS_LEFT) {
            active_corridor = CORRIDOR_NS;
        } else {
            active_corridor = CORRIDOR_EW;
        }
        for (int c = 0; c < CORRIDOR_COUNT; c++) {
            if (c == active_corridor) {
                ctrl->waiting_seconds[c] = 0;
            } else if (snap.raw_scores[c] > 0) {
                ctrl->waiting_seconds[c]++;
            } else {
                ctrl->waiting_seconds[c] = 0;
            }
        }

        /* 周期开始时计算绿灯时长 */
        if (ctrl->current_phase_elapsed == 0) {
            ctrl->current_stage_duration = plan_green_duration(
                phase_for_tick, &snap, &ctrl->config);
            stage_duration = ctrl->current_stage_duration;
        }

        /* 决策切换（安全模式下不切） */
        SwitchDecision sw_dec = {0};
        if (!ctrl->safe_mode) {
            decide_next_phase(ctrl, &snap, &sw_dec);
        }
        ctrl->current_phase_elapsed++;

        if (sw_dec.should_switch) {
            ctrl->pending_green_phase = sw_dec.target_phase;
            ctrl->current_phase = PHASE_YELLOW;
            ctrl->current_phase_elapsed = 0;
            strncpy(ctrl->last_reason, sw_dec.reason, sizeof(ctrl->last_reason) - 1);
            ctrl->switch_count++;
        }
    }
    /* === 黄灯 === */
    else if (phase_for_tick == PHASE_YELLOW) {
        ctrl->current_phase_elapsed++;
        if (ctrl->current_phase_elapsed >= ctrl->config.yellow_time) {
            ctrl->current_phase = PHASE_ALL_RED;
            ctrl->current_phase_elapsed = 0;
            snprintf(ctrl->last_reason, sizeof(ctrl->last_reason),
                     "clearance before %s",
                     phase_to_string(ctrl->pending_green_phase));
        }
    }
    /* === 全红 === */
    else if (phase_for_tick == PHASE_ALL_RED) {
        ctrl->current_phase_elapsed++;
        if (ctrl->current_phase_elapsed >= ctrl->config.all_red_time) {
            ctrl->current_phase = ctrl->pending_green_phase;
            ctrl->current_phase_elapsed = 0;
            ctrl->current_stage_duration = ctrl->config.base_green;
            snprintf(ctrl->last_reason, sizeof(ctrl->last_reason),
                     "entering %s", phase_to_string(ctrl->current_phase));
        }
    }

    /* === 填充决策 === */
    dec.phase = phase_for_tick;
    dec.duration = stage_duration;
    strncpy(dec.reason, reason, sizeof(dec.reason) - 1);
    dec.reason[sizeof(dec.reason) - 1] = '\0';
    dec.phase_elapsed = ctrl->current_phase_elapsed;

    for (int r = 0; r < ROAD_COUNT; r++) {
        for (int l = 0; l < LANE_COUNT; l++) {
            dec.road_lanes[r][l] = snap.road_lanes[r][l];
        }
    }
    for (int c = 0; c < CORRIDOR_COUNT; c++) {
        dec.corridor_counts[c] = snap.corridor_counts[c];
        dec.waiting_seconds[c] = ctrl->waiting_seconds[c];
    }
    dec.allowed_lanes_mask = controller_get_allowed_lanes(ctrl, phase_for_tick);

    return dec;
}

/* ========================================================================
 * 紧急模式
 * ======================================================================== */

void controller_set_emergency(TrafficController *ctrl, EmergencyMode mode) {
    if (!ctrl) return;
    ctrl->emergency_mode = mode;
}

void controller_report_comm_ok(TrafficController *ctrl) {
    if (!ctrl) return;
    ctrl->comm_lost_ticks = 0;
    if (ctrl->safe_mode) {
        ctrl->safe_mode = 0;
    }
}

int controller_get_safe_mode(const TrafficController *ctrl) {
    if (!ctrl) return 0;
    return ctrl->safe_mode;
}

SignalPhase controller_get_current_phase(const TrafficController *ctrl) {
    if (!ctrl) return PHASE_NS_STRAIGHT;
    return ctrl->current_phase;
}

/* ========================================================================
 * 内部队列步进（演示场景使用）
 *
 * 从 ctrl->lane_queues 构建 VehicleState 数组、调用核心决策逻辑。
 *
 * **不**自动放行：板子手推演示必须由操作员按"通过"键显式报告。
 * 控制器根据队列状态决定相位切换，但实际移除车辆只能通过
 * controller_report_pass()。
 * ======================================================================== */

SignalDecision controller_step_internal(TrafficController *ctrl) {
    if (!ctrl) {
        SignalDecision empty = {0};
        return empty;
    }

    /* 从内部队列构建 VehicleState 数组 */
    VehicleState vehicles[MAX_INTERNAL_VEHICLES];
    int n = 0;
    for (int r = 0; r < ROAD_COUNT && n < MAX_INTERNAL_VEHICLES; r++) {
        for (int l = 0; l < LANE_COUNT && n < MAX_INTERNAL_VEHICLES; l++) {
            int q = ctrl->lane_queues[r][l];
            for (int i = 0; i < q && n < MAX_INTERNAL_VEHICLES; i++) {
                vehicles[n].road = r;
                vehicles[n].lane = l;
                vehicles[n].position = 5.0f + i * 5.0f;
                vehicles[n].speed = 0.0f;
                vehicles[n].is_waiting = 1;
                snprintf(vehicles[n].vehicle_id, sizeof(vehicles[n].vehicle_id),
                         "Q-%d-%d-%d", r, l, i);
                n++;
            }
        }
    }

    /* 调用核心 step — 只做决策，不修改队列 */
    return controller_step(ctrl, vehicles, n);
}

/* ========================================================================
 * 内部队列步进（演示场景使用）
 *
 * 从 ctrl->lane_queues 构建 VehicleState 数组、调用核心逻辑、
 * 然后按当前相位许可自动放行。
 * ======================================================================== */

/**
 * @brief 报告一辆车到达（操作员按"到达"键）
 */
int controller_report_vehicle(TrafficController *ctrl, int road, int lane) {
    if (!ctrl) return -1;
    if (road < 0 || road >= ROAD_COUNT) return -1;
    if (lane < 0 || lane >= LANE_COUNT) return -1;
    ctrl->lane_queues[road][lane]++;
    return ctrl->lane_queues[road][lane];
}

/**
 * @brief 报告一辆车通过（操作员按"通过"键）
 *
 * 从队列中移除一辆车。设计原则：不能靠速度判断车是否通过——
 * 板子手推很慢，控制器以为车过了结果车还在，那就麻烦了。
 * 必须由操作员目视确认后手动触发。
 */
int controller_report_pass(TrafficController *ctrl, int road, int lane) {
    if (!ctrl) return -1;
    if (road < 0 || road >= ROAD_COUNT) return -1;
    if (lane < 0 || lane >= LANE_COUNT) return -1;
    if (ctrl->lane_queues[road][lane] > 0) {
        ctrl->lane_queues[road][lane]--;
    }
    return ctrl->lane_queues[road][lane];
}

/**
 * @brief 获取某车道当前队列长度
 */
int controller_get_queue_length(const TrafficController *ctrl, int road, int lane) {
    if (!ctrl) return 0;
    if (road < 0 || road >= ROAD_COUNT) return 0;
    if (lane < 0 || lane >= LANE_COUNT) return 0;
    return ctrl->lane_queues[road][lane];
}

/**
 * @brief 按当前相位放行
 */
void controller_discharge(TrafficController *ctrl, int rate_per_lane) {
    if (!ctrl) return;
    if (rate_per_lane <= 0) rate_per_lane = 1;

    int mask = controller_get_allowed_lanes(ctrl, ctrl->current_phase);

    /* 检查 12 个方向 × 动作的 bit */
    for (int r = 0; r < ROAD_COUNT; r++) {
        for (int l = 0; l < LANE_COUNT; l++) {
            int bit = r * 3 + l;
            if ((mask & (1 << bit)) != 0) {
                if (ctrl->lane_queues[r][l] > 0) {
                    int n = ctrl->lane_queues[r][l] >= rate_per_lane
                            ? rate_per_lane : ctrl->lane_queues[r][l];
                    ctrl->lane_queues[r][l] -= n;
                }
            }
        }
    }
}

/* ========================================================================
 * 辅助
 * ======================================================================== */

const char* phase_to_string(SignalPhase phase) {
    if (phase < 0 || phase >= PHASE_COUNT) return "?";
    return phase_names[phase];
}

const char* road_to_string(int road) {
    if (road < 0 || road >= ROAD_COUNT) return "?";
    return road_names[road];
}

const char* lane_to_string(int lane) {
    if (lane < 0 || lane >= LANE_COUNT) return "?";
    return lane_names[lane];
}

const char* corridor_to_string(int corridor) {
    if (corridor < 0 || corridor >= CORRIDOR_COUNT) return "?";
    return corridor_names[corridor];
}