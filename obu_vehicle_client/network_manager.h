#ifndef NETWORK_MANAGER_H
#define NETWORK_MANAGER_H

#include <stdbool.h>

typedef struct {
    const char *ssid;
    const char *password;
} NetworkConfig;

typedef struct {
    bool initialized;
    bool connected;
    bool connect_started;
    bool waiting_log_printed;
    unsigned long last_check_ms;
    unsigned long last_log_ms;
    NetworkConfig config;
} NetworkManager;

bool network_manager_init(NetworkManager *manager, const NetworkConfig *config);
bool network_manager_connect(NetworkManager *manager);
bool network_manager_is_connected(const NetworkManager *manager);
void network_manager_poll(NetworkManager *manager);

#endif
