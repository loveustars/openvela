/**
 * @file comm.c
 * @brief 通信接口实现 - MQTT + 模拟模式
 */

#include "comm.h"
#include "signal_controller.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

/* 模拟模式最大车辆数 */
#define SIM_MAX_VEHICLES 64

/* MQTT 配置默认值 */
static const MqttConfig default_mqtt_config = {
    .broker_host  = "192.168.1.100", /* TODO: 实际部署时替换为 Broker IP */
    .broker_port  = 1883,
    .client_id    = "openvela_rsu_001",
    .username     = "rsu",
    .password     = "rsu123",
    .keepalive_sec = 60,
    .use_tls      = false
};

/* 全局状态 */
static struct {
    MqttConfig         config;
    ConnectionState    state;
    MqttMessageCallback msg_cb;
    MqttConnectCallback conn_cb;
    CommStats          stats;

    /* 模拟模式数据 */
    bool               sim_mode;
    SimVehicle         sim_vehicles[SIM_MAX_VEHICLES];
    int                sim_vehicle_count;

    /* MQTT 重连状态 */
    bool               mqtt_initialized;
} g_comm;

/* 初始化全局状态 */
static void reset_comm_state(void) {
    g_comm.state = CONN_STATE_DISCONNECTED;
    g_comm.msg_cb = NULL;
    g_comm.conn_cb = NULL;
    g_comm.stats.state = CONN_STATE_DISCONNECTED;
    g_comm.stats.publish_count = 0;
    g_comm.stats.subscribe_count = 0;
    g_comm.stats.error_count = 0;
    g_comm.stats.reconnect_count = 0;
    g_comm.stats.last_payload_len = 0;
    g_comm.stats.last_msg_timestamp = 0;

    g_comm.sim_mode = false;
    g_comm.sim_vehicle_count = 0;
    g_comm.mqtt_initialized = false;
}

/* 初始化通信模块 */
int comm_init(const MqttConfig *config) {
    reset_comm_state();

    if (config) {
        memcpy(&g_comm.config, config, sizeof(MqttConfig));
    } else {
        memcpy(&g_comm.config, &default_mqtt_config, sizeof(MqttConfig));
    }

    /* TODO: 在实际 openvela 环境中初始化 MQTT 客户端库
     * 例如: mqtt_client_init(&g_comm.config);
     * 这里先假设 MQTT 库可用
     */
    g_comm.mqtt_initialized = true;

    return 0;
}

/* 启动通信 */
int comm_start(void) {
    if (g_comm.state == CONN_STATE_CONNECTED) {
        return 0; /* 已经连接 */
    }

    g_comm.state = CONN_STATE_CONNECTING;

    /* TODO: 实际 MQTT 连接
     * mqtt_connect(&g_comm.config);
     */
    printf("[COMM] Connecting to MQTT broker %s:%d ...\n",
           g_comm.config.broker_host, g_comm.config.broker_port);

    /* 模拟连接成功 */
    g_comm.state = CONN_STATE_CONNECTED;
    g_comm.stats.state = CONN_STATE_CONNECTED;

    if (g_comm.conn_cb) {
        g_comm.conn_cb(true);
    }

    printf("[COMM] Connected successfully.\n");
    return 0;
}

/* 停止通信 */
void comm_stop(void) {
    if (g_comm.state != CONN_STATE_DISCONNECTED) {
        /* TODO: 实际断开 MQTT
         * mqtt_disconnect();
         */
        g_comm.state = CONN_STATE_DISCONNECTED;
        g_comm.stats.state = CONN_STATE_DISCONNECTED;

        if (g_comm.conn_cb) {
            g_comm.conn_cb(false);
        }
    }
}

/* 发布消息 */
int comm_publish(const char *topic, const uint8_t *payload, int len) {
    if (!topic || !payload || len <= 0) {
        return -1;
    }

    if (g_comm.state != CONN_STATE_CONNECTED) {
        printf("[COMM] Warning: publish failed, not connected.\n");
        g_comm.stats.error_count++;
        return -1;
    }

    /* TODO: 实际 MQTT 发布
     * mqtt_publish(topic, payload, len);
     */
    printf("[COMM] PUB [%s] %d bytes\n", topic, len);

    g_comm.stats.publish_count++;
    g_comm.stats.last_payload_len = len;
    g_comm.stats.last_msg_timestamp = (int64_t)time(NULL) * 1000;

    return 0;
}

/* 订阅主题 */
int comm_subscribe(const char *topic) {
    if (!topic) return -1;

    if (g_comm.state != CONN_STATE_CONNECTED) {
        printf("[COMM] Warning: subscribe failed, not connected.\n");
        return -1;
    }

    /* TODO: 实际 MQTT 订阅
     * mqtt_subscribe(topic);
     */
    printf("[COMM] SUB [%s]\n", topic);

    g_comm.stats.subscribe_count++;
    return 0;
}

/* 消息回调触发(由网络层调用) */
void comm_on_message(const char *topic, const uint8_t *payload, int len) {
    if (g_comm.msg_cb && payload && len > 0) {
        g_comm.msg_cb(topic, payload, len);
    }
}

/* 设置回调 */
void comm_set_message_callback(MqttMessageCallback cb) {
    g_comm.msg_cb = cb;
}

void comm_set_connect_callback(MqttConnectCallback cb) {
    g_comm.conn_cb = cb;
}

/* 状态检查 */
bool comm_is_connected(void) {
    return (g_comm.state == CONN_STATE_CONNECTED);
}

/* 统计信息 */
const CommStats* comm_get_stats(void) {
    return &g_comm.stats;
}

void comm_reset_stats(void) {
    memset(&g_comm.stats, 0, sizeof(CommStats));
    g_comm.stats.state = g_comm.state;
}

/* 销毁 */
void comm_fini(void) {
    comm_stop();
    reset_comm_state();
}

/* ============ 模拟数据接口 ============ */

/* 激活/关闭模拟模式 */
void comm_set_simulation_mode(bool enable) {
    g_comm.sim_mode = enable;
    if (enable) {
        printf("[COMM] Simulation mode ENABLED.\n");
    } else {
        comm_clear_vehicle_data();
        printf("[COMM] Simulation mode DISABLED.\n");
    }
}

bool comm_is_simulation_mode(void) {
    return g_comm.sim_mode;
}

/* 注入模拟车辆数据 */
int comm_inject_vehicle_data(const SimVehicle *vehicles, int count) {
    if (!vehicles || count <= 0) {
        return -1;
    }

    if (count > SIM_MAX_VEHICLES) {
        printf("[COMM] Warning: too many vehicles, truncating to %d\n", SIM_MAX_VEHICLES);
        count = SIM_MAX_VEHICLES;
    }

    g_comm.sim_vehicle_count = count;
    for (int i = 0; i < count; i++) {
        memcpy(&g_comm.sim_vehicles[i], &vehicles[i], sizeof(SimVehicle));
    }

    return 0;
}

/* 清除模拟数据 */
void comm_clear_vehicle_data(void) {
    g_comm.sim_vehicle_count = 0;
    memset(g_comm.sim_vehicles, 0, sizeof(g_comm.sim_vehicles));
}

/* 获取模拟车辆数据(供控制器step使用) */
const SimVehicle* comm_get_sim_vehicles(int *count) {
    if (count) *count = g_comm.sim_vehicle_count;
    return g_comm.sim_vehicles;
}

/* ============ JSON 解析工具 ============ */

/* 简单字符串解析: 提取 JSON 中的字符串值 */
static int extract_json_string(const char *json, const char *key, char *out, int out_size) {
    if (!json || !key || !out || out_size <= 0) return -1;

    char pattern[64];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);

    const char *p = strstr(json, pattern);
    if (!p) return -1;

    p = strchr(p, ':');
    if (!p) return -1;
    p++;

    /* 跳过空白 */
    while (*p == ' ' || *p == '\t') p++;

    /* 提取字符串值 */
    if (*p == '"') {
        p++;
        const char *end = strchr(p, '"');
        if (!end) return -1;

        int len = (int)(end - p);
        if (len >= out_size) len = out_size - 1;
        strncpy(out, p, len);
        out[len] = '\0';
        return 0;
    }
    return -1;
}

/* 简单字符串解析: 提取 JSON 中的整数值 */
static int extract_json_int(const char *json, const char *key, int *out) {
    if (!json || !key || !out) return -1;

    char pattern[64];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);

    const char *p = strstr(json, pattern);
    if (!p) return -1;

    p = strchr(p, ':');
    if (!p) return -1;
    p++;

    /* 跳过空白 */
    while (*p == ' ' || *p == '\t') p++;

    /* 提取整数值 */
    *out = atoi(p);
    return 0;
}

/* 解析 SPaT JSON */
int comm_parse_spat_json(const char *json, char *phase_out, int *remain_out) {
    if (!json) return -1;

    if (phase_out) {
        if (extract_json_string(json, "phase", phase_out, 16) != 0) {
            /* 尝试小写 */
            if (extract_json_string(json, "phase_name", phase_out, 16) != 0) {
                return -1;
            }
        }
    }

    if (remain_out) {
        if (extract_json_int(json, "remain", remain_out) != 0) {
            extract_json_int(json, "remaining", remain_out);
        }
    }

    return 0;
}

/* 构建 SPaT JSON */
int comm_build_spat_json(char *buf, int buf_size, const char *phase, int remain, const char *rsu_id) {
    if (!buf || !phase || !rsu_id) return -1;

    int len = snprintf(buf, (size_t)buf_size,
        "{"
        "\"rsu_id\":\"%s\","
        "\"phase\":\"%s\","
        "\"remain\":%d,"
        "\"timestamp\":%lld"
        "}",
        rsu_id,
        phase,
        remain,
        (long long)(time(NULL))
    );

    if (len >= buf_size) return -1;
    return 0;
}