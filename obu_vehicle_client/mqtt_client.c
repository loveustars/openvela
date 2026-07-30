#include "mqtt_client.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "config.h"

static bool mqtt_client_copy_payload(
    const char *payload,
    size_t payload_len,
    char *buffer,
    size_t buffer_size)
{
    if (payload == NULL || buffer == NULL || payload_len == 0 || payload_len >= buffer_size) {
        return false;
    }

    memcpy(buffer, payload, payload_len);
    buffer[payload_len] = '\0';
    return true;
}

static bool extract_json_string(const char *json, const char *key, char *out, size_t out_size)
{
    char pattern[32];
    const char *pos;
    const char *start;
    const char *end;
    size_t length;

    if (json == NULL || key == NULL || out == NULL || out_size == 0) {
        return false;
    }

    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    pos = strstr(json, pattern);
    if (pos == NULL) {
        return false;
    }

    pos = strchr(pos, ':');
    if (pos == NULL) {
        return false;
    }

    start = strchr(pos, '"');
    if (start == NULL) {
        return false;
    }

    ++start;
    end = strchr(start, '"');
    if (end == NULL) {
        return false;
    }

    length = (size_t)(end - start);
    if (length >= out_size) {
        length = out_size - 1;
    }

    memcpy(out, start, length);
    out[length] = '\0';
    return true;
}

static bool extract_json_int(const char *json, const char *key, int *out)
{
    char pattern[32];
    const char *pos;

    if (json == NULL || key == NULL || out == NULL) {
        return false;
    }

    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    pos = strstr(json, pattern);
    if (pos == NULL) {
        return false;
    }

    pos = strchr(pos, ':');
    if (pos == NULL) {
        return false;
    }

    ++pos;
    while (*pos == ' ' || *pos == '\t') {
        ++pos;
    }

    *out = atoi(pos);
    return true;
}

static bool extract_json_ulong(const char *json, const char *key, unsigned long *out)
{
    char pattern[32];
    const char *pos;

    if (json == NULL || key == NULL || out == NULL) {
        return false;
    }

    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    pos = strstr(json, pattern);
    if (pos == NULL) {
        return false;
    }

    pos = strchr(pos, ':');
    if (pos == NULL) {
        return false;
    }

    ++pos;
    while (*pos == ' ' || *pos == '\t') {
        ++pos;
    }

    *out = strtoul(pos, NULL, 10);
    return true;
}

static bool mqtt_client_is_valid_phase(const char *phase)
{
    return strcmp(phase, "green") == 0
        || strcmp(phase, "yellow") == 0
        || strcmp(phase, "red") == 0
        || strcmp(phase, "NS_STRAIGHT") == 0
        || strcmp(phase, "NS_LEFT") == 0
        || strcmp(phase, "EW_STRAIGHT") == 0
        || strcmp(phase, "EW_LEFT") == 0
        || strcmp(phase, "YELLOW") == 0
        || strcmp(phase, "ALL_RED") == 0;
}

bool mqtt_client_load_runtime_config(MqttRuntimeConfig *config)
{
    FILE *fp;
    char line[128];
    bool host_found = false;
    bool port_found = false;

    if (config == NULL) {
        return false;
    }

    memset(config, 0, sizeof(*config));
    strncpy(config->host, MQTT_BROKER_HOST, sizeof(config->host) - 1);
    config->port = MQTT_BROKER_PORT;

    fp = fopen(MQTT_CONFIG_PATH, "r");
    if (fp == NULL) {
        return true;
    }

    while (fgets(line, sizeof(line), fp) != NULL) {
        line[strcspn(line, "\n")] = 0;
        if (strncmp(line, "HOST=", 5) == 0) {
            strncpy(config->host, line + 5, sizeof(config->host) - 1);
            host_found = true;
        } else if (strncmp(line, "PORT=", 5) == 0) {
            config->port = atoi(line + 5);
            port_found = true;
        }
    }

    fclose(fp);

    if (!host_found) {
        strncpy(config->host, MQTT_BROKER_HOST, sizeof(config->host) - 1);
    }

    if (!port_found || config->port <= 0) {
        config->port = MQTT_BROKER_PORT;
    }

    return config->host[0] != '\0';
}

static int mqtt_client_open_socket(const char *host, int port)
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
        printf("[MQTT] getaddrinfo failed: %s\n", gai_strerror(ret));
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

static void mqtt_client_publish_callback(void **state, struct mqtt_response_publish *publish)
{
    MqttClient *client = state != NULL ? (MqttClient *)(*state) : NULL;
    char topic[MQTT_TOPIC_MAX_LEN];
    size_t topic_len;

    if (client == NULL || publish == NULL) {
        return;
    }

    topic_len = publish->topic_name_size;
    if (topic_len >= sizeof(topic)) {
        topic_len = sizeof(topic) - 1;
    }

    memcpy(topic, publish->topic_name, topic_len);
    topic[topic_len] = '\0';

    printf("[MQTT] recv topic=%s bytes=%u\n",
           topic, (unsigned int)publish->application_message_size);

    if (client->message_callback != NULL) {
        (void)client->message_callback(
            topic,
            (const char *)publish->application_message,
            publish->application_message_size,
            client->message_user_data);
    }
}

static void *mqtt_client_sync_thread(void *arg)
{
    MqttClient *client = (MqttClient *)arg;

    while (1) {
        pthread_mutex_lock(&client->lock);
        if (client->connected) {
            enum MQTTErrors err = mqtt_sync(&client->client);
            if (err != MQTT_OK || client->client.error != MQTT_OK) {
                printf("[MQTT] sync error=%s client_error=%s\n",
                       mqtt_error_str(err),
                       mqtt_error_str(client->client.error));
                client->connected = false;
                client->subscribed = false;
                if (client->socket_fd >= 0) {
                    close(client->socket_fd);
                    client->socket_fd = -1;
                }
            }
        }
        pthread_mutex_unlock(&client->lock);
        usleep(100000U);
    }

    return NULL;
}

bool mqtt_client_init(MqttClient *client, const MqttClientConfig *config)
{
    if (client == NULL || config == NULL) {
        return false;
    }

    memset(client, 0, sizeof(*client));
    client->config = *config;
    client->initialized = true;
    client->socket_fd = -1;
    pthread_mutex_init(&client->lock, NULL);

    printf("[MQTT] init client_id=%s host=%s port=%d\n",
        client->config.client_id,
        client->config.host,
        client->config.port);
    return true;
}

bool mqtt_client_connect(MqttClient *client, unsigned long now_ms)
{
    enum MQTTErrors err;
    int socket_fd;

    if (client == NULL || !client->initialized) {
        return false;
    }

    client->last_connect_attempt_ms = now_ms;

    socket_fd = mqtt_client_open_socket(client->config.host, client->config.port);
    if (socket_fd < 0) {
        printf("[MQTT] connect socket failed host=%s port=%d\n",
               client->config.host, client->config.port);
        client->connected = false;
        return false;
    }

    pthread_mutex_lock(&client->lock);
    client->socket_fd = socket_fd;
    err = mqtt_init(&client->client,
                    socket_fd,
                    client->sendbuf, sizeof(client->sendbuf),
                    client->recvbuf, sizeof(client->recvbuf),
                    mqtt_client_publish_callback);
    if (err == MQTT_OK) {
        client->client.publish_response_callback_state = client;
        err = mqtt_connect(&client->client,
                           client->config.client_id,
                           NULL,
                           NULL,
                           0,
                           client->config.username,
                           client->config.password,
                           MQTT_CONNECT_CLEAN_SESSION,
                           MQTT_KEEPALIVE_SECONDS);
    }

    if (err != MQTT_OK || client->client.error != MQTT_OK) {
        printf("[MQTT] connect failed err=%s client_error=%s\n",
               mqtt_error_str(err),
               mqtt_error_str(client->client.error));
        close(socket_fd);
        client->socket_fd = -1;
        client->connected = false;
        pthread_mutex_unlock(&client->lock);
        return false;
    }

    client->connected = true;
    client->subscribed = false;
    pthread_mutex_unlock(&client->lock);

    if (!client->thread_started) {
        if (pthread_create(&client->sync_thread, NULL, mqtt_client_sync_thread, client) != 0) {
            pthread_mutex_lock(&client->lock);
            close(client->socket_fd);
            client->socket_fd = -1;
            client->connected = false;
            pthread_mutex_unlock(&client->lock);
            return false;
        }
        client->thread_started = true;
    }

    printf("[MQTT] connect ok host=%s port=%d\n",
        client->config.host,
        client->config.port);
    return true;
}

bool mqtt_client_is_connected(const MqttClient *client)
{
    return client != NULL && client->connected;
}

bool mqtt_client_publish(MqttClient *client, const char *topic, const char *payload)
{
    enum MQTTErrors err;

    if (client == NULL || !client->connected || topic == NULL || payload == NULL) {
        return false;
    }

    pthread_mutex_lock(&client->lock);
    err = mqtt_publish(&client->client, topic, payload, strlen(payload), MQTT_PUBLISH_QOS_0);
    pthread_mutex_unlock(&client->lock);

    if (err != MQTT_OK) {
        printf("[MQTT] publish failed topic=%s err=%s\n", topic, mqtt_error_str(err));
        return false;
    }

    printf("[MQTT] topic=%s\n", topic);
    printf("[MQTT] payload=%s\n", payload);
    return true;
}

bool mqtt_client_subscribe(MqttClient *client, const char *topic)
{
    enum MQTTErrors err;

    if (client == NULL || !client->initialized || !client->connected || topic == NULL) {
        return false;
    }

    if (strlen(topic) >= sizeof(client->subscribed_topic)) {
        return false;
    }

    pthread_mutex_lock(&client->lock);
    err = mqtt_subscribe(&client->client, topic, 0);
    pthread_mutex_unlock(&client->lock);
    if (err != MQTT_OK) {
        printf("[MQTT] subscribe failed topic=%s err=%s\n", topic, mqtt_error_str(err));
        return false;
    }

    memset(client->subscribed_topic, 0, sizeof(client->subscribed_topic));
    strncpy(client->subscribed_topic, topic, sizeof(client->subscribed_topic) - 1);
    client->subscribed = true;

    printf("[MQTT] subscribe topic=%s\n", client->subscribed_topic);
    return true;
}

void mqtt_client_set_message_callback(
    MqttClient *client,
    MqttMessageCallback callback,
    void *user_data)
{
    if (client == NULL) {
        return;
    }

    client->message_callback = callback;
    client->message_user_data = user_data;
}

bool mqtt_client_on_message(
    MqttClient *client,
    const char *topic,
    const char *payload,
    size_t payload_len)
{
    if (client == NULL || !client->initialized || topic == NULL || payload == NULL || payload_len == 0) {
        return false;
    }

    if (client->message_callback == NULL) {
        return true;
    }

    return client->message_callback(topic, payload, payload_len, client->message_user_data);
}

bool mqtt_client_parse_spat_json(
    const char *payload,
    size_t payload_len,
    SpatMessage *message)
{
    char json[MQTT_PAYLOAD_BUFFER_SIZE];

    if (message == NULL) {
        return false;
    }

    memset(message, 0, sizeof(*message));
    if (!mqtt_client_copy_payload(payload, payload_len, json, sizeof(json))) {
        return false;
    }

    if (!extract_json_string(json, "phase", message->phase, sizeof(message->phase))) {
        return false;
    }

    if (!mqtt_client_is_valid_phase(message->phase)) {
        return false;
    }

    if (!extract_json_int(json, "remain", &message->remain_s)) {
        return false;
    }

    if (message->remain_s < 0) {
        return false;
    }

    (void)extract_json_string(json, "light_id", message->light_id, sizeof(message->light_id));
    (void)extract_json_ulong(json, "timestamp", &message->timestamp);
    return true;
}

void mqtt_client_poll(MqttClient *client, unsigned long now_ms)
{
    if (client == NULL || !client->initialized) {
        return;
    }

    if (client->connected) {
        return;
    }

    if (now_ms - client->last_connect_attempt_ms < MQTT_RECONNECT_INTERVAL_MS) {
        return;
    }

    printf("[MQTT] reconnect attempt\n");
    (void)mqtt_client_connect(client, now_ms);
}

void mqtt_client_disconnect(MqttClient *client)
{
    if (client == NULL) {
        return;
    }

    pthread_mutex_lock(&client->lock);
    if (client->connected) {
        (void)mqtt_disconnect(&client->client);
    }
    client->connected = false;
    client->subscribed = false;
    if (client->socket_fd >= 0) {
        close(client->socket_fd);
        client->socket_fd = -1;
    }
    pthread_mutex_unlock(&client->lock);
    printf("[MQTT] disconnect\n");
}
