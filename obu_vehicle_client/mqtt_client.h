#ifndef MQTT_CLIENT_H
#define MQTT_CLIENT_H

#include <stdbool.h>
#include <stddef.h>
#include <pthread.h>

#include <mqtt.h>

#include "config.h"

typedef struct {
    const char *client_id;
    const char *host;
    int port;
    const char *username;
    const char *password;
} MqttClientConfig;

typedef struct {
    char host[64];
    int port;
} MqttRuntimeConfig;

typedef struct {
    char light_id[16];
    char phase[16];
    int remain_s;
    unsigned long timestamp;
} SpatMessage;

typedef bool (*MqttMessageCallback)(
    const char *topic,
    const char *payload,
    size_t payload_len,
    void *user_data);

typedef struct {
    bool initialized;
    bool connected;
    bool subscribed;
    bool thread_started;
    unsigned long last_connect_attempt_ms;
    MqttClientConfig config;
    char subscribed_topic[MQTT_TOPIC_MAX_LEN];
    MqttMessageCallback message_callback;
    void *message_user_data;
    struct mqtt_client client;
    int socket_fd;
    pthread_t sync_thread;
    pthread_mutex_t lock;
    uint8_t sendbuf[MQTT_SEND_BUFFER_SIZE];
    uint8_t recvbuf[MQTT_RECV_BUFFER_SIZE];
} MqttClient;

bool mqtt_client_init(MqttClient *client, const MqttClientConfig *config);
bool mqtt_client_connect(MqttClient *client, unsigned long now_ms);
bool mqtt_client_is_connected(const MqttClient *client);
bool mqtt_client_publish(MqttClient *client, const char *topic, const char *payload);
bool mqtt_client_subscribe(MqttClient *client, const char *topic);
void mqtt_client_set_message_callback(
    MqttClient *client,
    MqttMessageCallback callback,
    void *user_data);
bool mqtt_client_on_message(
    MqttClient *client,
    const char *topic,
    const char *payload,
    size_t payload_len);
bool mqtt_client_parse_spat_json(
    const char *payload,
    size_t payload_len,
    SpatMessage *message);

bool mqtt_client_load_runtime_config(MqttRuntimeConfig *config);
void mqtt_client_poll(MqttClient *client, unsigned long now_ms);
void mqtt_client_disconnect(MqttClient *client);

#endif
