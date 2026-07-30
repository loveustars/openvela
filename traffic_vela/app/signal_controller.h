/**
 * @file signal_controller.h
 * @brief 三车道版智能信号控制器
 *
 * 4 个独立相位 + 安全模式（参考 NEMA 双环结构简化）：
 *
 * ## 信号相位
 *
 * - NS_STRAIGHT  : N/S 直行+右转（同时放行）
 * - NS_LEFT      : N/S 左转（独立相位）
 * - EW_STRAIGHT  : E/W 直行+右转
 * - EW_LEFT      : E/W 左转
 * - YELLOW       : 黄灯过渡
 * - ALL_RED      : 全红清空
 *
 * ## 通行许可
 *
 * 12 个独立方向 × 动作组合的掩码（4 方向 × 3 动作 = 12 bit）：
 *
 *   bit 0:  N  左转
 *   bit 1:  N  直行
 *   bit 2:  N  右转
 *   bit 3:  S  左转
 *   bit 4:  S  直行
 *   bit 5:  S  右转
 *   bit 6:  E  左转
 *   bit 7:  E  直行
 *   bit 8:  E  右转
 *   bit 9:  W  左转
 *   bit 10: W  直行
 *   bit 11: W  右转
 *
 * @author OpenVela Traffic Team
 * @version 2.0.0
 */

#ifndef SIGNAL_CONTROLLER_H
#define SIGNAL_CONTROLLER_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================
 * 基本枚举
 * ======================================================================== */

#define ROAD_N  0
#define ROAD_S  1
#define ROAD_E  2
#define ROAD_W  3
#define ROAD_COUNT 4

#define LANE_LEFT     0
#define LANE_STRAIGHT 1
#define LANE_RIGHT    2
#define LANE_COUNT    3

#define CORRIDOR_NS  0
#define CORRIDOR_EW  1
#define CORRIDOR_COUNT 2

/**
 * @brief 信号相位
 */
typedef enum {
    PHASE_NS_STRAIGHT = 0, /**< N/S 直行+右转 */
    PHASE_NS_LEFT     = 1, /**< N/S 左转 */
    PHASE_EW_STRAIGHT = 2, /**< E/W 直行+右转 */
    PHASE_EW_LEFT     = 3, /**< E/W 左转 */
    PHASE_YELLOW      = 4, /**< 黄灯过渡 */
    PHASE_ALL_RED     = 5, /**< 全红清空 */
    PHASE_COUNT
} SignalPhase;

/**
 * @brief 紧急模式
 */
typedef enum {
    EMERGENCY_NONE      = 0,
    EMERGENCY_COMM_LOST = 1  /**< 通信中断，进入安全模式 */
} EmergencyMode;

/* ========================================================================
 * 通行许可位掩码
 *
 * 12 bit 掩码：bit (road * 3 + lane) 表示 (road, lane) 是否允许通行
 * ======================================================================== */

#define PERMIT_N_LEFT     (1 << 0)
#define PERMIT_N_STRAIGHT (1 << 1)
#define PERMIT_N_RIGHT    (1 << 2)
#define PERMIT_S_LEFT     (1 << 3)
#define PERMIT_S_STRAIGHT (1 << 4)
#define PERMIT_S_RIGHT    (1 << 5)
#define PERMIT_E_LEFT     (1 << 6)
#define PERMIT_E_STRAIGHT (1 << 7)
#define PERMIT_E_RIGHT    (1 << 8)
#define PERMIT_W_LEFT     (1 << 9)
#define PERMIT_W_STRAIGHT (1 << 10)
#define PERMIT_W_RIGHT    (1 << 11)

/* 各方向所有右转（位掩码） */
#define RIGHT_MASK_N  PERMIT_N_RIGHT
#define RIGHT_MASK_S  PERMIT_S_RIGHT
#define RIGHT_MASK_E  PERMIT_E_RIGHT
#define RIGHT_MASK_W  PERMIT_W_RIGHT
#define RIGHT_MASK_ALL (RIGHT_MASK_N | RIGHT_MASK_S | RIGHT_MASK_E | RIGHT_MASK_W)

/** 内部最大车辆数（演示场景使用）*/
#define MAX_INTERNAL_VEHICLES 128

/* ========================================================================
 * 数据结构
 * ======================================================================== */

typedef struct {
    char  vehicle_id[32];
    int   road;
    int   lane;
    float position;
    float speed;
    bool  is_waiting;
} VehicleState;

typedef struct {
    int total;
    int waiting;
} LaneStats;

typedef struct {
    int   base_green;
    int   min_green;
    int   max_green;
    int   yellow_time;
    int   all_red_time;
    int   left_green_min;
    int   left_green_max;
    int   release_rate_per_road;

    float vehicle_weight;
    float waiting_bonus;
    float stopped_queue_bonus;
    float proximity_bonus;
    float proximity_range;
    float starvation_weight;
    int   green_adjustment_window;
} ControllerConfig;

typedef struct {
    SignalPhase phase;
    int         duration;
    char        reason[128];
    int         phase_elapsed;

    LaneStats road_lanes[ROAD_COUNT][LANE_COUNT];

    int   corridor_counts[CORRIDOR_COUNT];
    int   waiting_seconds[CORRIDOR_COUNT];

    /** 12 位通行许可掩码 */
    int allowed_lanes_mask;
} SignalDecision;

typedef struct {
    ControllerConfig config;
    SignalPhase current_phase;
    int current_phase_elapsed;
    int current_stage_duration;
    SignalPhase pending_green_phase;
    int waiting_seconds[CORRIDOR_COUNT];
    char last_reason[128];
    EmergencyMode emergency_mode;
    int comm_lost_ticks;
    int max_comm_lost_ticks;
    int lane_queues[ROAD_COUNT][LANE_COUNT];
    int tick_count;
    int switch_count;

    /* 防饥饿 */
    int ticks_since_ns_straight;
    int ticks_since_ns_left;
    int ticks_since_ew_straight;
    int ticks_since_ew_left;
    int safe_mode;
} TrafficController;

/* ========================================================================
 * 初始化与配置
 * ======================================================================== */

void controller_get_default_config(ControllerConfig *config);

void controller_init(TrafficController *ctrl,
                     const ControllerConfig *config,
                     SignalPhase initial_phase);

void controller_reset(TrafficController *ctrl);

/* ========================================================================
 * 主控制逻辑
 * ======================================================================== */

SignalDecision controller_step(TrafficController *ctrl,
                                const VehicleState *vehicles,
                                int vehicle_count);

/**
 * @brief 简化步进：仅使用控制器内部队列
 *
 * 适用于板子演示场景：板子已通过 controller_report_vehicle() 报告过车，
 * 每 tick 调用此函数即可。
 */
SignalDecision controller_step_internal(TrafficController *ctrl);

/* ========================================================================
 * 紧急与通信处理
 * ======================================================================== */

void controller_set_emergency(TrafficController *ctrl, EmergencyMode mode);
void controller_report_comm_ok(TrafficController *ctrl);
int  controller_get_safe_mode(const TrafficController *ctrl);
SignalPhase controller_get_current_phase(const TrafficController *ctrl);

/* ========================================================================
 * 车辆报告与队列管理
 * ======================================================================== */

/**
 * @brief 报告一辆车到达路口（板子推过来时按"到达"键）
 *
 * 板子手推演示场景：操作员在板子到达路口时按一次"到达"键。
 * 控制器把车加入队列，并在当前相位（或下一相位）允许时放行。
 *
 * 重要：控制器不靠速度判断车是否通过——那会导致"板子还没动灯就切了"的问题。
 * "通过"必须由操作员显式调用 controller_report_pass() 报告。
 *
 * @param ctrl      控制器
 * @param road      道路 (ROAD_N/S/E/W)
 * @param lane      车道 (LANE_LEFT/STRAIGHT/RIGHT)
 * @return int      当前该车道队列长度
 */
int controller_report_vehicle(TrafficController *ctrl, int road, int lane);

/**
 * @brief 报告一辆车已通过路口（板子完全离开路口时按"通过"键）
 *
 * 板子手推演示场景：操作员目视确认板子已通过停车线时按"通过"键。
 * 控制器从队列移除该车（按先入先出顺序）。
 *
 * 如果队列为空，此操作为 no-op。
 *
 * @param ctrl      控制器
 * @param road      道路 (ROAD_N/S/E/W)
 * @param lane      车道 (LANE_LEFT/STRAIGHT/RIGHT)
 * @return int      移除后该车道队列长度（-1 表示车道无效）
 */
int controller_report_pass(TrafficController *ctrl, int road, int lane);

/**
 * @brief 获取某车道当前队列长度
 */
int controller_get_queue_length(const TrafficController *ctrl, int road, int lane);

/**
 * @brief 手动放行（仿真器/测试用）
 *
 * 在 controller_step 内部会自动放行，但也可以手动调用。
 */
void controller_discharge(TrafficController *ctrl, int rate_per_lane);

/* ========================================================================
 * 通行许可查询
 * ======================================================================== */

/** @brief 获取指定相位的通行许可位掩码 */
int controller_get_allowed_lanes(const TrafficController *ctrl, SignalPhase phase);

/** @brief 检查车辆是否允许通行（根据 12-bit 许可掩码） */
bool controller_can_pass(const TrafficController *ctrl,
                        const VehicleState *vehicle,
                        SignalDecision *decision);

/* ========================================================================
 * 辅助函数
 * ======================================================================== */

const char* phase_to_string(SignalPhase phase);
const char* road_to_string(int road);
const char* lane_to_string(int lane);
const char* corridor_to_string(int corridor);

#ifdef __cplusplus
}
#endif

#endif /* SIGNAL_CONTROLLER_H */