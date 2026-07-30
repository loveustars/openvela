/**
 * @file simulator.h
 * @brief 三车道交通模拟器
 *
 * 提供开发调试用的内置模拟器，支持生成各种交通场景的车辆数据。
 *
 * 模拟器按固定周期生成车辆并维护各车道队列，可与信号控制器联动运行。
 *
 * ## 场景说明
 *
 * | 场景 | 说明 |
 * |------|------|
 * | balanced | 南北/东西流量均衡，三车道均匀分布 |
 * | ns_rush | 南北方向高峰期，左转需求较大 |
 * | ew_rush | 东西方向高峰期，左转需求较大 |
 * | left_rush | 各方向左转需求明显 |
 * | alternating_wave | 前30秒NS高峰，后30秒EW高峰 |
 * | idle | 无车辆输入（空载测试）|
 *
 * @author OpenVela Traffic Team
 * @version 1.1.0
 */

#ifndef SIMULATOR_H
#define SIMULATOR_H

#include <stdint.h>
#include <stdbool.h>

#include "comm.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 场景类型枚举
 */
typedef enum {
    SCENARIO_BALANCED        = 0, /**< 均衡场景 */
    SCENARIO_NS_RUSH         = 1, /**< 南北高峰 */
    SCENARIO_EW_RUSH         = 2, /**< 东西高峰 */
    SCENARIO_LEFT_RUSH       = 3, /**< 左转高峰 */
    SCENARIO_ALTERNATING_WAVE = 4, /**< 交替波状 */
    SCENARIO_IDLE            = 5, /**< 空载 */
    SCENARIO_COUNT
} ScenarioType;

/**
 * @brief 模拟器配置
 */
typedef struct {
    ScenarioType scenario;           /**< 场景类型 */
    int duration_seconds;            /**< 仿真总时长（秒）*/
    int tick_interval_ms;            /**< 每次tick间隔（毫秒）*/
} SimulatorConfig;

/**
 * @brief 三车道模拟器实例
 *
 * 维护四个方向、三条车道的队列状态。
 */
typedef struct {
    SimulatorConfig config;              /**< 配置参数 */
    int current_tick;                    /**< 当前tick计数 */
    int lane_queues[4][3];               /**< 车道队列 [道路][车道] */
    int vehicle_sequence;                 /**< 车辆编号递增计数器 */
    bool running;                        /**< 运行状态 */
} Simulator;

/**
 * @brief 创建模拟器
 * @param[in] config 配置参数（NULL使用默认）
 * @return Simulator* 模拟器指针，失败返回NULL
 *
 * 示例：
 * @code
 * SimulatorConfig cfg = {
 *     .scenario = SCENARIO_NS_RUSH,
 *     .duration_seconds = 60,
 *     .tick_interval_ms = 1000
 * };
 * Simulator *sim = simulator_create(&cfg);
 * @endcode
 */
Simulator* simulator_create(const SimulatorConfig *config);

/**
 * @brief 销毁模拟器
 * @param sim 模拟器指针
 */
void simulator_destroy(Simulator *sim);

/**
 * @brief 重置模拟器状态
 * @param sim 模拟器指针
 *
 * 清除所有队列，重置tick计数和车辆编号。
 */
void simulator_reset(Simulator *sim);

/**
 * @brief 执行一步模拟
 * @param[in] sim 模拟器指针
 * @param[out] vehicles_out 车辆输出数组
 * @param[in] max_count 最大车辆数量
 * @return int 实际生成车辆数量
 *
 * 每秒调用一次，执行：
 * 1. 根据当前场景生成新到达车辆入队
 * 2. 将队列中车辆转换为 VehicleState 列表
 *
 * 示例：
 * @code
 * SimVehicle vehicles[64];
 * int count = simulator_step(sim, vehicles, 64);
 * for (int i = 0; i < count; i++) {
 *     printf("%s lane %d\n", vehicles[i].vehicle_id, vehicles[i].lane);
 * }
 * @endcode
 */
int simulator_step(Simulator *sim, SimVehicle *vehicles_out, int max_count);

/**
 * @brief 根据当前相位放行车辆
 * @param sim 模拟器指针
 * @param phase 当前信号相位
 * @param release_rate 每tick每道路放行数量
 *
 * 根据信号相位从对应方向的车道队列中移除车辆：
 * - NS_ALL_GO: 放行 N/S 的直行+右转车道
 * - EW_ALL_GO: 放行 E/W 的直行+右转车道
 * - LEFT_ONLY: 放行所有方向的左转车道
 * - 黄灯/全红：不放行
 *
 * @note 右转车道始终可通行，但此处按相位放行逻辑处理。
 *       实际通行判断应使用 controller_can_pass()。
 */
void simulator_discharge(Simulator *sim, int phase, int release_rate);

/**
 * @brief 获取当前各车道队列长度
 * @param[in] sim 模拟器指针
 * @param[out] queues_out 队列输出数组 [4][3]
 *
 * 示例：
 * @code
 * int queues[4][3];
 * simulator_get_lane_queues(sim, queues);
 * printf("N左转: %d, N直行: %d, N右转: %d\n",
 *        queues[0][0], queues[0][1], queues[0][2]);
 * @endcode
 */
void simulator_get_lane_queues(const Simulator *sim, int queues_out[4][3]);

/**
 * @brief 获取场景名称
 * @param[in] type 场景类型
 * @return const char* 场景名称（如 "ns_rush", "left_rush"）
 */
const char* simulator_scenario_name(ScenarioType type);

/**
 * @brief 获取场景描述
 * @param[in] type 场景类型
 * @return const char* 场景描述中文
 */
const char* simulator_scenario_desc(ScenarioType type);

#ifdef __cplusplus
}
#endif

#endif /* SIMULATOR_H */