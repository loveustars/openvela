#include "network_manager.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "config.h"
#include "platform_adapter.h"
#define WIFI_STATUS_LOG_INTERVAL_MS 5000UL

bool network_manager_init(NetworkManager *manager, const NetworkConfig *config)
{
    if (manager == NULL || config == NULL) {
        return false;
    }

    memset(manager, 0, sizeof(*manager));
    manager->config = *config;
    manager->initialized = true;
    return true;
}

bool network_manager_connect(NetworkManager *manager)
{
    unsigned long now_ms;

    if (manager == NULL || !manager->initialized) {
        return false;
    }

    now_ms = platform_adapter_get_time_ms();

    if (!platform_adapter_network_ready()) {
        if (now_ms - manager->last_log_ms >= WIFI_STATUS_LOG_INTERVAL_MS) {
            printf("[NET] network not ready yet\n");
            manager->last_log_ms = now_ms;
        }
        manager->connected = false;
        return false;
    }

    if (!manager->connect_started) {
        printf("[NET] manual wifi mode, use nsh to connect wlan0\n");
        printf("[NET] example: ifup wlan0; wapi mode wlan0 2; wapi psk wlan0 <password> 3 2; wapi essid wlan0 <ssid> 1; renew wlan0\n");
        manager->connect_started = true;
        manager->last_log_ms = now_ms;
        manager->waiting_log_printed = false;
    }

    manager->connected = (access(WIFI_MANUAL_READY_FLAG, F_OK) == 0);
    if (manager->connected) {
        manager->waiting_log_printed = false;
        if (now_ms - manager->last_log_ms >= WIFI_STATUS_LOG_INTERVAL_MS) {
            printf("[NET] manual wifi ready flag detected, MQTT can start now\n");
            manager->last_log_ms = now_ms;
        }
        return true;
    }

    if (!manager->waiting_log_printed) {
        printf("[NET] wifi not armed yet, connect in nsh then run: echo ready > " WIFI_MANUAL_READY_FLAG "\n");
        manager->last_log_ms = now_ms;
        manager->waiting_log_printed = true;
    }
    return false;
}

bool network_manager_is_connected(const NetworkManager *manager)
{
    return manager != NULL && manager->connected;
}

void network_manager_poll(NetworkManager *manager)
{
    if (manager == NULL || !manager->initialized) {
        return;
    }

    if (!manager->connected) {
        (void)network_manager_connect(manager);
    }
}
