/**
 * @file gui_server.h
 * @brief 嵌入式 HTTP 服务器 - 为浏览器 GUI 提供实时数据
 *
 * 运行真实的 C 信号控制器和模拟器，通过 HTTP JSON API 供前端访问。
 *
 * ## HTTP API
 *
 * - `GET /`          返回 index.html 页面
 * - `GET /api/state` 返回当前信号状态 JSON
 *
 * ## 状态 JSON 格式
 *
 * @code
 * {
 *   "phase": 0,
 *   "phase_name": "NS_ALL_GO",
 *   "phase_elapsed": 5,
 *   "stage_duration": 20,
 *   "switch_count": 3,
 *   "current_tick": 25,
 *   "allowed_lanes_mask": 6,
 *   "safe_mode": 0,
 *   "cycles_since_ns": 0,
 *   "cycles_since_ew": 2,
 *   "lane_queues": [[3,5,0],[2,4,0],[0,0,0],[1,2,0]],
 *   "corridor_counts": [14, 3],
 *   "waiting_seconds": [0, 12]
 * }
 * @endcode
 *
 * Compile with:
 * gcc -Wall -std=c99 -o gui_server gui_main.c src/gui_server.c
 *     src/signal_controller.c src/simulator.c src/comm.c -I./include
 *
 * @author OpenVela Traffic Team
 * @version 1.2.0
 */

#ifndef GUI_SERVER_H
#define GUI_SERVER_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 服务器配置
 */
typedef struct {
    int port;              /**< 监听端口，默认 8080 */
    int tick_interval_ms;  /**< 模拟器步进间隔（毫秒），默认 500 */
    int max_clients;       /**< 最大客户端数，默认 4 */
} ServerConfig;

/**
 * @brief 启动 HTTP 服务器
 *
 * 在当前线程中运行事件循环，处理 HTTP 请求并更新模拟器状态。
 * 该函数会一直阻塞，直到收到终止信号或发生致命错误。
 *
 * @param config 服务器配置（NULL 使用默认配置）
 * @return 仅在服务器异常终止时返回（返回 -1）
 */
int gui_server_start(const ServerConfig *config);

/**
 * @brief 停止 HTTP 服务器
 *
 * 在下一个事件循环迭代中优雅退出。
 */
void gui_server_stop(void);

#ifdef __cplusplus
}
#endif

#endif /* GUI_SERVER_H */