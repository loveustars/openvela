/**
 * @file comm.c
 * @brief 通信接口实现 - MQTT + 模拟模式
 */

#include "comm.h"
#include "signal_controller.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

/* 模拟模式最大车辆数 */
#define SIM_MAX_VEHICLES 64
#define MQTT_SEND_BUFFER_SIZE 2048
#define MQTT_RECV_BUFFER_SIZE 1024
#define MQTT_RECONNECT_INTERVAL_MS 2000UL

/* MQTT 配置默认值 */
static const MqttConfig default_mqtt_config = {
    .broker_host  = "192.168.1.100",
    .broker_port  = 1883,
    .client_id    = "openvela_rsu_001",
    .username     = "",
    .password     = "",
    .keepalive_sec = 60,
    .use_tls      = false
};

/* 全局状态 */
static struct {
    MqttConfig          config;
    ConnectionState     state;
    MqttMessageCallback msg_cb;
    MqttConnectCallback conn_cb;
    CommStats           stats;

    bool                sim_mode;
    SimVehicle          sim_vehicles[SIM_MAX_VEHICLES];
    int                 sim_vehicle_count;

    bool                mqtt_initialized;
    bool                thread_started;
    unsigned long       last_connect_attempt_ms;
    int                 socket_fd;
    char                subscribed_topics[4][64];
    int                 subscribed_topic_count;
    struct mqtt_client  client;
    pthread_t           sync_thread;
    pthread_mutex_t     lock;
    uint8_t             sendbuf[MQTT_SEND_BUFFER_SIZE];
    uint8_t             recvbuf[MQTT_RECV_BUFFER_SIZE];
} g_comm;

static void reset_comm_state(void)
{
    memset(&g_comm, 0, sizeof(g_comm));
    g_comm.state = CONN_STATE_DISCONNECTED;
    g_comm.stats.state = CONN_STATE_DISCONNECTED;
    g_comm.socket_fd = -1;
}

static int open_socket(const char *host, int port)
{
    struct addrinfo hints;
    struct addrinfo *servinfo;
    struct addrinfo *itr;
    char port_str[8];
    int fd = -1;
    int ret;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    snprintf(port_str, sizeof(port_str), "%d", port);
    ret = getaddrinfo(host, port_str, &hints, &servinfo);
    if (ret != 0) {
        printf("[COMM] getaddrinfo failed: %s\n", gai_strerror(ret));
        return -1;
    }

    for (itr = servinfo; itr != NULL; itr = itr->ai_next) {
        fd = socket(itr->ai_family, itr->ai_socktype, itr->ai_protocol);
        if (fd < 0) {
            continue;
        }

        ret = connect(fd, itr->ai_addr, itr->ai_addrlen);
        if (ret == 0) {
            break;
        }

        close(fd);
        fd = -1;
    }

    freeaddrinfo(servinfo);
    if (fd < 0) {
        return -1;
    }

    ret = fcntl(fd, F_GETFL, 0);
    if (ret < 0 || fcntl(fd, F_SETFL, ret | O_NONBLOCK) < 0) {
        close(fd);
        return -1;
    }

    return fd;
}

static void publish_callback(void **state, struct mqtt_response_publish *publish)
{
    char topic[64];
    size_t topic_len;

    if (publish == NULL) {
        return;
    }

    topic_len = publish->topic_name_size;
    if (topic_len >= sizeof(topic)) {
        topic_len = sizeof(topic) - 1;
    }

    memcpy(topic, publish->topic_name, topic_len);
    topic[topic_len] = '\0';

    printf("[COMM] recv topic=%s bytes=%u\n",
           topic, (unsigned int)publish->application_message_size);

    if (g_comm.msg_cb != NULL) {
        g_comm.msg_cb(topic,
                      (const uint8_t *)publish->application_message,
                      (int)publish->application_message_size);
    }
}

static void *sync_thread_fn(void *arg)
{
    (void)arg;

    while (1) {
        pthread_mutex_lock(&g_comm.lock);
        if (g_comm.state == CONN_STATE_CONNECTED) {
            enum MQTTErrors err = mqtt_sync(&g_comm.client);
            if (err != MQTT_OK || g_comm.client.error != MQTT_OK) {
                printf("[COMM] sync error=%s client_error=%s\n",
                       mqtt_error_str(err),
                       mqtt_error_str(g_comm.client.error));
                g_comm.state = CONN_STATE_DISCONNECTED;
                g_comm.stats.state = CONN_STATE_DISCONNECTED;
                g_comm.subscribed_topic_count = 0;
                if (g_comm.socket_fd >= 0) {
                    close(g_comm.socket_fd);
                    g_comm.socket_fd = -1;
                }
            }
        }
        pthread_mutex_unlock(&g_comm.lock);
        usleep(100000U);
    }

    return NULL;
}

static int extract_json_string(const char *json, const char *key, char *out, int out_size)
{
    char pattern[64];
    const char *p;
    const char *start;
    const char *end;
    int len;

    if (!json || !key || !out || out_size <= 0) {
        return -1;
    }

    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    p = strstr(json, pattern);
    if (!p) {
        return -1;
    }

    p = strchr(p, ':');
    if (!p) {
        return -1;
    }

    start = strchr(p, '"');
    if (!start) {
        return -1;
    }

    start++;
    end = strchr(start, '"');
    if (!end) {
        return -1;
    }

    len = (int)(end - start);
    if (len >= out_size) {
        len = out_size - 1;
    }

    strncpy(out, start, len);
    out[len] = '\0';
    return 0;
}

static int extract_json_int(const char *json, const char *key, int *out)
{
    char pattern[64];
    const char *p;

    if (!json || !key || !out) {
        return -1;
    }

    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    p = strstr(json, pattern);
    if (!p) {
        return -1;
    }

    p = strchr(p, ':');
    if (!p) {
        return -1;
    }

    p++;
    while (*p == ' ' || *p == '\t') {
        p++;
    }

    *out = atoi(p);
    return 0;
}

static int extract_json_ulong(const char *json, const char *key, unsigned long *out)
{
    char pattern[64];
    const char *p;

    if (!json || !key || !out) {
        return -1;
    }

    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    p = strstr(json, pattern);
    if (!p) {
        return -1;
    }

    p = strchr(p, ':');
    if (!p) {
        return -1;
    }

    p++;
    while (*p == ' ' || *p == '\t') {
        p++;
    }

    *out = strtoul(p, NULL, 10);
    return 0;
}

int comm_init(const MqttConfig *config)
{
    reset_comm_state();

    if (config) {
        memcpy(&g_comm.config, config, sizeof(MqttConfig));
    } else {
        memcpy(&g_comm.config, &default_mqtt_config, sizeof(MqttConfig));
    }

    pthread_mutex_init(&g_comm.lock, NULL);
    g_comm.mqtt_initialized = true;
    return 0;
}

int comm_start(void)
{
    enum MQTTErrors err;
    int socket_fd;

    if (g_comm.state == CONN_STATE_CONNECTED) {
        return 0;
    }

    g_comm.state = CONN_STATE_CONNECTING;
    g_comm.stats.state = CONN_STATE_CONNECTING;

    printf("[COMM] Connecting to MQTT broker %s:%d ...\n",
           g_comm.config.broker_host, g_comm.config.broker_port);

    socket_fd = open_socket(g_comm.config.broker_host, g_comm.config.broker_port);
    if (socket_fd < 0) {
        g_comm.state = CONN_STATE_DISCONNECTED;
        g_comm.stats.state = CONN_STATE_DISCONNECTED;
        g_comm.stats.error_count++;
        return -1;
    }

    pthread_mutex_lock(&g_comm.lock);
    g_comm.socket_fd = socket_fd;
    err = mqtt_init(&g_comm.client,
                    socket_fd,
                    g_comm.sendbuf, sizeof(g_comm.sendbuf),
                    g_comm.recvbuf, sizeof(g_comm.recvbuf),
                    publish_callback);
    if (err == MQTT_OK) {
        g_comm.client.publish_response_callback_state = NULL;
        err = mqtt_connect(&g_comm.client,
                           g_comm.config.client_id,
                           NULL,
                           NULL,
                           0,
                           g_comm.config.username,
                           g_comm.config.password,
                           MQTT_CONNECT_CLEAN_SESSION,
                           g_comm.config.keepalive_sec);
    }

    if (err != MQTT_OK || g_comm.client.error != MQTT_OK) {
        pthread_mutex_unlock(&g_comm.lock);
        close(socket_fd);
        g_comm.socket_fd = -1;
        g_comm.state = CONN_STATE_DISCONNECTED;
        g_comm.stats.state = CONN_STATE_DISCONNECTED;
        g_comm.stats.error_count++;
        printf("[COMM] connect failed err=%s client_error=%s\n",
               mqtt_error_str(err),
               mqtt_error_str(g_comm.client.error));
        return -1;
    }

    g_comm.state = CONN_STATE_CONNECTED;
    g_comm.stats.state = CONN_STATE_CONNECTED;
    pthread_mutex_unlock(&g_comm.lock);

    if (!g_comm.thread_started) {
        if (pthread_create(&g_comm.sync_thread, NULL, sync_thread_fn, NULL) != 0) {
            pthread_mutex_lock(&g_comm.lock);
            close(g_comm.socket_fd);
            g_comm.socket_fd = -1;
            g_comm.state = CONN_STATE_DISCONNECTED;
            g_comm.stats.state = CONN_STATE_DISCONNECTED;
            pthread_mutex_unlock(&g_comm.lock);
            return -1;
        }
        g_comm.thread_started = true;
    }

    if (g_comm.conn_cb) {
        g_comm.conn_cb(true);
    }

    printf("[COMM] Connected successfully.\n");
    return 0;
}

void comm_stop(void)
{
    pthread_mutex_lock(&g_comm.lock);
    if (g_comm.state == CONN_STATE_CONNECTED) {
        (void)mqtt_disconnect(&g_comm.client);
    }
    g_comm.state = CONN_STATE_DISCONNECTED;
    g_comm.stats.state = CONN_STATE_DISCONNECTED;
    if (g_comm.socket_fd >= 0) {
        close(g_comm.socket_fd);
        g_comm.socket_fd = -1;
    }
    pthread_mutex_unlock(&g_comm.lock);

    if (g_comm.conn_cb) {
        g_comm.conn_cb(false);
    }
}

int comm_publish(const char *topic, const uint8_t *payload, int len)
{
    enum MQTTErrors err;

    if (!topic || !payload || len <= 0) {
        return -1;
    }

    if (g_comm.state != CONN_STATE_CONNECTED) {
        g_comm.stats.error_count++;
        return -1;
    }

    pthread_mutex_lock(&g_comm.lock);
    err = mqtt_publish(&g_comm.client, topic, payload, (size_t)len, MQTT_PUBLISH_QOS_0);
    pthread_mutex_unlock(&g_comm.lock);

    if (err != MQTT_OK) {
        printf("[COMM] publish failed topic=%s err=%s\n", topic, mqtt_error_str(err));
        g_comm.stats.error_count++;
        return -1;
    }

    printf("[COMM] PUB [%s] %d bytes\n", topic, len);
    printf("[COMM] payload=%.*s\n", len, (const char *)payload);
    g_comm.stats.publish_count++;
    g_comm.stats.last_payload_len = len;
    g_comm.stats.last_msg_timestamp = (int64_t)time(NULL) * 1000;
    return 0;
}

int comm_subscribe(const char *topic)
{
    enum MQTTErrors err;

    if (!topic || g_comm.state != CONN_STATE_CONNECTED) {
        return -1;
    }

    pthread_mutex_lock(&g_comm.lock);
    err = mqtt_subscribe(&g_comm.client, topic, 0);
    pthread_mutex_unlock(&g_comm.lock);

    if (err != MQTT_OK) {
        printf("[COMM] subscribe failed topic=%s err=%s\n", topic, mqtt_error_str(err));
        g_comm.stats.error_count++;
        return -1;
    }

    if (g_comm.subscribed_topic_count < (int)(sizeof(g_comm.subscribed_topics) / sizeof(g_comm.subscribed_topics[0]))) {
        strncpy(g_comm.subscribed_topics[g_comm.subscribed_topic_count],
                topic,
                sizeof(g_comm.subscribed_topics[g_comm.subscribed_topic_count]) - 1);
        g_comm.subscribed_topic_count++;
    }

    printf("[COMM] SUB [%s]\n", topic);
    g_comm.stats.subscribe_count++;
    return 0;
}

void comm_on_message(const char *topic, const uint8_t *payload, int len)
{
    if (g_comm.msg_cb && payload && len > 0) {
        g_comm.msg_cb(topic, payload, len);
    }
}

void comm_set_message_callback(MqttMessageCallback cb)
{
    g_comm.msg_cb = cb;
}

void comm_set_connect_callback(MqttConnectCallback cb)
{
    g_comm.conn_cb = cb;
}

bool comm_is_connected(void)
{
    return g_comm.state == CONN_STATE_CONNECTED;
}

const CommStats* comm_get_stats(void)
{
    return &g_comm.stats;
}

void comm_reset_stats(void)
{
    memset(&g_comm.stats, 0, sizeof(CommStats));
    g_comm.stats.state = g_comm.state;
}

void comm_fini(void)
{
    comm_stop();
}

void comm_set_simulation_mode(bool enable)
{
    g_comm.sim_mode = enable;
    if (enable) {
        printf("[COMM] Simulation mode ENABLED.\n");
    } else {
        comm_clear_vehicle_data();
        printf("[COMM] Simulation mode DISABLED.\n");
    }
}

bool comm_is_simulation_mode(void)
{
    return g_comm.sim_mode;
}

int comm_inject_vehicle_data(const SimVehicle *vehicles, int count)
{
    if (!vehicles || count <= 0) {
        return -1;
    }

    if (count > SIM_MAX_VEHICLES) {
        count = SIM_MAX_VEHICLES;
    }

    g_comm.sim_vehicle_count = count;
    for (int i = 0; i < count; i++) {
        memcpy(&g_comm.sim_vehicles[i], &vehicles[i], sizeof(SimVehicle));
    }

    return 0;
}

void comm_clear_vehicle_data(void)
{
    g_comm.sim_vehicle_count = 0;
    memset(g_comm.sim_vehicles, 0, sizeof(g_comm.sim_vehicles));
}

const SimVehicle* comm_get_sim_vehicles(int *count)
{
    if (count) {
        *count = g_comm.sim_vehicle_count;
    }
    return g_comm.sim_vehicles;
}

int comm_parse_spat_json(const char *json, char *phase_out, int *remain_out)
{
    if (!json) {
        return -1;
    }

    if (phase_out) {
        if (extract_json_string(json, "phase", phase_out, 16) != 0) {
            return -1;
        }
    }

    if (remain_out) {
        if (extract_json_int(json, "remain", remain_out) != 0) {
            return -1;
        }
    }

    return 0;
}

int comm_build_spat_json(char *buf, int buf_size, const char *phase, int remain, const char *light_id)
{
    int len;

    if (!buf || !phase || !light_id) {
        return -1;
    }

    len = snprintf(buf, (size_t)buf_size,
                   "{"
                   "\"light_id\":\"%s\","
                   "\"phase\":\"%s\","
                   "\"remain\":%d,"
                   "\"timestamp\":%lld"
                   "}",
                   light_id,
                   phase,
                   remain,
                   (long long)time(NULL));

    if (len <= 0 || len >= buf_size) {
        return -1;
    }

    return 0;
}

int comm_parse_vehicle_json(const char *json, OBUVehicleMessage *message)
{
    if (!json || !message) {
        return -1;
    }

    memset(message, 0, sizeof(*message));
    if (extract_json_string(json, "vehicle_id", message->vehicle_id, sizeof(message->vehicle_id)) != 0) {
        return -1;
    }

    if (extract_json_string(json, "from_direction", message->from_direction, sizeof(message->from_direction)) != 0) {
        return -1;
    }

    if (extract_json_string(json, "to_direction", message->to_direction, sizeof(message->to_direction)) != 0) {
        return -1;
    }

    (void)extract_json_string(json, "status", message->status, sizeof(message->status));
    (void)extract_json_ulong(json, "timestamp", &message->timestamp);
    message->is_pass_event = strcmp(message->status, "passed") == 0;
    return 0;
}

void comm_poll(unsigned long now_ms)
{
    if (g_comm.state == CONN_STATE_CONNECTED) {
        return;
    }

    if (now_ms - g_comm.last_connect_attempt_ms < MQTT_RECONNECT_INTERVAL_MS) {
        return;
    }

    g_comm.last_connect_attempt_ms = now_ms;
    printf("[COMM] reconnect attempt\n");
    (void)comm_start();
}
