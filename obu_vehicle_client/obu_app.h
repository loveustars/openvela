#ifndef OBU_APP_H
#define OBU_APP_H

#include <stdbool.h>
#include <stddef.h>

#include "mqtt_client.h"
#include "network_manager.h"
#include "spat_state.h"
#include "vehicle_gui.h"
#include "vehicle_state.h"

typedef struct {
    NetworkManager network;
    MqttClient mqtt;
    MqttRuntimeConfig runtime_mqtt;
    VehicleState vehicle;
    SpatState spat;
    VehicleGuiState gui;
    bool spat_subscription_done;
    bool pending_enter_publish;
    bool pending_leave_publish;
} ObuApp;

bool obu_app_init(ObuApp *app);
void obu_app_step(ObuApp *app, unsigned long now_ms);
bool obu_app_handle_spat_message(
    ObuApp *app,
    const char *topic,
    const char *payload,
    size_t payload_len);
bool obu_app_debug_feed_spat(ObuApp *app, const char *topic, const char *payload);

#endif
