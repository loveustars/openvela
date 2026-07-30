/**
 * @file comm.h
 * @brief 通信接口封装
 *
 * 封装 MQTT 通信层，提供给队友 A 调用。
 *
 * ## 接口说明
 *
 * **MQTT 发布/订阅**（由队友 A 负责封装）：
 * - comm_publish() / comm_subscribe()
 * - 网络断开时自动重连
 *
 * **模拟数据注入**（用于开发测试）：
 * - comm_set_simulation_mode()
 * - comm_inject_vehicle_data()
 *
 * @author OpenVela Traffic Team
 * @version 1.1.0
 */

#ifndef COMM_H
#define COMM_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <pthread.h>

#include <mqtt.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================
 * MQTT 相关类型
 * ======================================================================== */

/**
 * @brief MQTT 消息回调
 * @param[in] topic      主题
 * @param[in] payload   消息内容
 * @param[in] payload_len 消息长度
 */
typedef void (*MqttMessageCallback)(const char *topic, const uint8_t *payload, int payload_len);

/**
 * @brief MQTT 连接状态回调
 * @param[in] connected 是否已连接
 */
typedef void (*MqttConnectCallback)(bool connected);

/**
 * @brief MQTT 配置
 */
typedef struct {
    const char *broker_host;  /**< Broker 地址 */
    int         broker_port;   /**< Broker 端口 */
    const char *client_id;     /**< 客户端ID */
    const char *username;     /**< 用户名 */
    const char *password;     /**< 密码 */
    int         keepalive_sec;/**< 保活间隔 */
    bool        use_tls;       /**< 是否使用 TLS */
} MqttConfig;

/**
 * @brief 连接状态枚举
 */
typedef enum {
    CONN_STATE_DISCONNECTED = 0, /**< 断开 */
    CONN_STATE_CONNECTING,        /**< 连接中 */
    CONN_STATE_CONNECTED,        /**< 已连接 */
    CONN_STATE_ERROR             /**< 错误 */
} ConnectionState;

/**
 * @brief 通信统计信息
 */
typedef struct {
    ConnectionState state;           /**< 当前连接状态 */
    int  publish_count;               /**< 发布消息计数 */
    int  subscribe_count;            /**< 订阅消息计数 */
    int  error_count;                /**< 错误计数 */
    int  reconnect_count;            /**< 重连次数 */
    int  last_payload_len;           /**< 最近一次消息长度 */
    int64_t last_msg_timestamp;     /**< 最近消息时间戳（毫秒）*/
} CommStats;

/* ========================================================================
 * 模拟车辆数据
 * ======================================================================== */

/**
 * @brief 模拟车辆数据结构（与信号控制器共用）
 *
 * 车道 lane: 0=LEFT, 1=STRAIGHT, 2=RIGHT
 */
typedef struct {
    char  vehicle_id[32];   /**< 车辆ID */
    int   road;             /**< 道路（0=N, 1=S, 2=E, 3=W）*/
    int   lane;             /**< 车道（0=LEFT, 1=STRAIGHT, 2=RIGHT）*/
    float position;        /**< 距停止线距离（米）*/
    float speed;           /**< 车速（m/s）*/
    bool  is_waiting;       /**< 是否等待 */
} SimVehicle;

typedef struct {
    char  vehicle_id[32];
    char  from_direction[8];
    char  to_direction[16];
    char  status[16];
    bool  is_pass_event;
    unsigned long timestamp;
} OBUVehicleMessage;

/* ========================================================================
 * MQTT 接口
 * ======================================================================== */

/**
 * @brief 初始化通信模块
 * @param[in] config MQTT配置（NULL使用默认）
 * @return int 0成功，-1失败
 *
 * 默认配置使用 192.168.1.100:1883，客户端ID为 openvela_rsu_001。
 */
int comm_init(const MqttConfig *config);

/**
 * @brief 启动 MQTT 连接
 * @return int 0成功，-1失败
 */
int comm_start(void);

/**
 * @brief 断开 MQTT 连接
 */
void comm_stop(void);

/**
 * @brief 发布消息
 * @param[in] topic   主题
 * @param[in] payload 消息内容
 * @param[in] len     消息长度
 * @return int 0成功，-1失败
 */
int comm_publish(const char *topic, const uint8_t *payload, int len);

/**
 * @brief 订阅主题
 * @param[in] topic 主题（支持通配符）
 * @return int 0成功，-1失败
 */
int comm_subscribe(const char *topic);

/**
 * @brief 设置消息回调
 */
void comm_set_message_callback(MqttMessageCallback cb);

/**
 * @brief 设置连接状态回调
 */
void comm_set_connect_callback(MqttConnectCallback cb);

/**
 * @brief 检查连接状态
 * @return bool 已连接返回true
 */
bool comm_is_connected(void);

/**
 * @brief 获取通信统计
 * @return const CommStats* 统计信息指针
 */
const CommStats* comm_get_stats(void);

/**
 * @brief 重置统计
 */
void comm_reset_stats(void);

/**
 * @brief 销毁通信模块
 */
void comm_fini(void);

/* ========================================================================
 * 模拟数据接口
 * ======================================================================== */

/**
 * @brief 激活/关闭模拟模式
 * @param[in] enable true开启模拟，false关闭
 *
 * 开启后不依赖真实 MQTT 连接，使用注入的模拟数据。
 *
 * 示例：
 * @code
 * comm_set_simulation_mode(true);
 * SimVehicle vehicles[4] = {...};
 * comm_inject_vehicle_data(vehicles, 4);
 * @endcode
 */
void comm_set_simulation_mode(bool enable);

/**
 * @brief 检查是否在模拟模式
 * @return bool 模拟模式返回true
 */
bool comm_is_simulation_mode(void);

/**
 * @brief 注入模拟车辆数据
 * @param[in] vehicles 车辆数组
 * @param[in] count     车辆数量
 * @return int 0成功
 *
 * 将外部生成的车辆数据注入通信层，供控制器 step() 使用。
 */
int comm_inject_vehicle_data(const SimVehicle *vehicles, int count);

/**
 * @brief 清除模拟数据
 */
void comm_clear_vehicle_data(void);

/**
 * @brief 获取注入的车辆数据
 * @param[out] count 车辆数量输出
 * @return const SimVehicle* 车辆数组指针
 */
const SimVehicle* comm_get_sim_vehicles(int *count);

/* ========================================================================
 * JSON 工具
 * ======================================================================== */

/**
 * @brief 解析 SPaT 报文
 * @param[in] json       JSON字符串
 * @param[out] phase_out 灯色输出（green/yellow/red）
 * @param[out] remain_out 剩余秒数输出
 * @return int 0成功，-1格式错误
 */
int comm_parse_spat_json(const char *json, char *phase_out, int *remain_out);

/**
 * @brief 构建 SPaT 报文
 * @param[out] buf       输出缓冲区
 * @param[in] buf_size   缓冲区大小
 * @param[in] phase      灯色（green/yellow/red）
 * @param[in] remain     剩余秒数
 * @param[in] rsu_id     设备ID
 * @return int 0成功
 *
 * 示例输出：
 * @code
 * {"rsu_id":"rsu_a","phase":"green","remain":12,"timestamp":1710000000}
 * @endcode
 */
int comm_build_spat_json(char *buf, int buf_size, const char *phase,
                         int remain, const char *light_id);

int comm_parse_vehicle_json(const char *json, OBUVehicleMessage *message);

void comm_poll(unsigned long now_ms);

#ifdef __cplusplus
}
#endif

#endif /* COMM_H */
