#include "obu_app.h"

#include <string.h>

#include "config.h"
#include "json_builder.h"
#include "logging.h"
#include "platform_adapter.h"
#include "topic_router.h"

static const char *kTag = "OBU";
static const char *kDefaultRsuTargets[] = {"rsu_a"};

static void obu_app_demo_update_spat(ObuApp *app, unsigned long now_ms)
{
#if OBU_DEMO_OFFLINE_MODE
    static const struct {
        const char *phase;
        int duration_s;
    } phases[] = {
        {"green", 12},
        {"yellow", 3},
        {"red", 8},
    };
    unsigned long elapsed_ms;
    unsigned long cycle_s;
    unsigned long total_cycle_s = 0;
    size_t i;

    if (app == NULL || !app->vehicle.has_entered_and_reported || app->vehicle.has_passed) {
        return;
    }

    elapsed_ms = now_ms - app->vehicle.entered_at_ms;
    cycle_s = (elapsed_ms / 1000UL);

    for (i = 0; i < sizeof(phases) / sizeof(phases[0]); i++) {
        total_cycle_s += (unsigned long)phases[i].duration_s;
    }
    if (total_cycle_s > 0) {
        cycle_s %= total_cycle_s;
    }

    for (i = 0; i < sizeof(phases) / sizeof(phases[0]); i++) {
        if (cycle_s < (unsigned long)phases[i].duration_s) {
            int remain = phases[i].duration_s - (int)cycle_s;
            if (spat_state_update(&app->spat,
                                  "light_a",
                                  phases[i].phase,
                                  remain,
                                  platform_adapter_get_unix_timestamp())) {
                app->gui.dirty = true;
            }
            return;
        }
        cycle_s -= (unsigned long)phases[i].duration_s;
    }
#else
    (void)app;
    (void)now_ms;
#endif
}

static bool obu_app_on_mqtt_message(
    const char *topic,
    const char *payload,
    size_t payload_len,
    void *user_data)
{
    return obu_app_handle_spat_message((ObuApp *)user_data, topic, payload, payload_len);
}

static bool obu_app_publish_vehicle_state(ObuApp *app)
{
#if OBU_DEMO_OFFLINE_MODE
    app->vehicle.has_entered_and_reported = true;
    app->vehicle.has_passed = false;
    app->vehicle.timestamp = platform_adapter_get_unix_timestamp();
    app->vehicle.entered_at_ms = platform_adapter_get_time_ms();
    (void)spat_state_update(&app->spat, "light_a", "green", 12, app->vehicle.timestamp);
    app->gui.dirty = true;
    log_info(kTag, "offline demo enter accepted");
    return true;
#else
    char payload[256];
    char topic[MQTT_TOPIC_MAX_LEN];
    bool published = false;
    size_t i;

    if (!json_builder_build_vehicle_state(&app->vehicle, payload, sizeof(payload))) {
        log_error(kTag, "build vehicle json failed");
        return false;
    }

    for (i = 0; i < sizeof(kDefaultRsuTargets) / sizeof(kDefaultRsuTargets[0]); i++) {
        if (!topic_router_build_vehicle_state_topic(
                kDefaultRsuTargets[i],
                topic,
                sizeof(topic))) {
            log_error(kTag, "build vehicle state topic failed");
            return false;
        }

        if (!mqtt_client_publish(&app->mqtt, topic, payload)) {
            log_warn(kTag, "publish failed target=%s", kDefaultRsuTargets[i]);
            continue;
        }

        published = true;
    }

    if (!published) {
        return false;
    }

    app->vehicle.has_entered_and_reported = true;
    log_info(
        kTag,
        "vehicle enter sent from=%s to=%s",
        app->vehicle.from_direction == CARDINAL_UNKNOWN ? "unknown" : app->vehicle.from_direction == CARDINAL_EAST ? "E" :
        app->vehicle.from_direction == CARDINAL_SOUTH ? "S" :
        app->vehicle.from_direction == CARDINAL_WEST ? "W" : "N",
        app->vehicle.turn_direction == TURN_LEFT ? "left" :
        app->vehicle.turn_direction == TURN_STRAIGHT ? "straight" :
        app->vehicle.turn_direction == TURN_RIGHT ? "right" : "unknown");
    return true;
#endif
}

static bool obu_app_publish_vehicle_pass(ObuApp *app)
{
#if OBU_DEMO_OFFLINE_MODE
    app->vehicle.has_passed = true;
    app->vehicle.has_entered_and_reported = false;
    app->vehicle.entered_at_ms = 0;
    (void)spat_state_update(&app->spat, "light_a", "red", 0, platform_adapter_get_unix_timestamp());
    app->gui.dirty = true;
    log_info(kTag, "offline demo leave accepted");
    return true;
#else
    char payload[256];
    char topic[MQTT_TOPIC_MAX_LEN];
    bool published = false;
    size_t i;

    if (!json_builder_build_vehicle_pass(&app->vehicle, payload, sizeof(payload))) {
        log_error(kTag, "build vehicle pass json failed");
        return false;
    }

    for (i = 0; i < sizeof(kDefaultRsuTargets) / sizeof(kDefaultRsuTargets[0]); i++) {
        if (!topic_router_build_vehicle_pass_topic(
                kDefaultRsuTargets[i],
                topic,
                sizeof(topic))) {
            log_error(kTag, "build vehicle pass topic failed");
            return false;
        }

        if (!mqtt_client_publish(&app->mqtt, topic, payload)) {
            log_warn(kTag, "publish pass failed target=%s", kDefaultRsuTargets[i]);
            continue;
        }

        published = true;
    }

    if (!published) {
        return false;
    }

    app->vehicle.has_passed = true;
    log_info(kTag, "vehicle pass sent");
    return true;
#endif
}

bool obu_app_init(ObuApp *app)
{
    NetworkConfig network_config = {
        .ssid = WIFI_SSID,
        .password = WIFI_PASSWORD,
    };
    MqttClientConfig mqtt_config;

    if (app == NULL) {
        return false;
    }

    memset(app, 0, sizeof(*app));
    vehicle_state_init(&app->vehicle);
    spat_state_init(&app->spat);
    vehicle_gui_init(&app->gui);
    app->spat_subscription_done = false;

    if (!mqtt_client_load_runtime_config(&app->runtime_mqtt)) {
        log_error(kTag, "load mqtt runtime config failed");
        return false;
    }

    memset(&mqtt_config, 0, sizeof(mqtt_config));
    mqtt_config.client_id = MQTT_CLIENT_ID;
    mqtt_config.host = app->runtime_mqtt.host;
    mqtt_config.port = app->runtime_mqtt.port;
    mqtt_config.username = MQTT_USERNAME;
    mqtt_config.password = MQTT_PASSWORD;

    if (!platform_adapter_gui_init(&app->gui, &app->vehicle, &app->spat)) {
        return false;
    }

    if (!network_manager_init(&app->network, &network_config)) {
        return false;
    }

    if (!mqtt_client_init(&app->mqtt, &mqtt_config)) {
        return false;
    }

    mqtt_client_set_message_callback(&app->mqtt, obu_app_on_mqtt_message, app);
    return true;
}

void obu_app_step(ObuApp *app, unsigned long now_ms)
{
    GuiInputEvent gui_input;

    if (platform_adapter_should_quit()) {
        platform_adapter_shutdown();
        return;
    }

    gui_input = platform_adapter_read_gui_input(now_ms);
    if (gui_input.select_from_direction) {
        vehicle_gui_select_from_direction(&app->gui, gui_input.from_direction);
    }
    if (gui_input.select_turn_direction) {
        vehicle_gui_select_turn_direction(&app->gui, gui_input.turn_direction);
    }

    if (gui_input.press_enter) {
        vehicle_gui_request_enter(&app->gui);
        app->pending_enter_publish = true;
        app->pending_leave_publish = false;
        app->gui.dirty = true;
    }
    if (gui_input.press_leave) {
        vehicle_gui_request_leave(&app->gui);
        app->pending_leave_publish = true;
        app->gui.dirty = true;
    }

    vehicle_state_set_route(
        &app->vehicle,
        app->gui.selected_from_direction,
        app->gui.selected_turn_direction);
    vehicle_state_update_timestamp(&app->vehicle, platform_adapter_get_unix_timestamp());

    vehicle_gui_render(&app->gui, &app->vehicle, &app->spat);
    platform_adapter_gui_refresh();

#if OBU_DEMO_OFFLINE_MODE
    obu_app_demo_update_spat(app, now_ms);
#else
    network_manager_poll(&app->network);
    if (!network_manager_is_connected(&app->network)) {
        return;
    }

    if (!mqtt_client_is_connected(&app->mqtt)) {
        (void)mqtt_client_connect(&app->mqtt, now_ms);
    }
    mqtt_client_poll(&app->mqtt, now_ms);

    if (!app->spat_subscription_done && mqtt_client_is_connected(&app->mqtt)) {
        app->spat_subscription_done = mqtt_client_subscribe(&app->mqtt, MQTT_SPAT_TOPIC);
    }

    if (platform_adapter_mock_receive_spat(now_ms, &app->spat)) {
        app->gui.dirty = true;
    }
#endif

    if (app->pending_enter_publish &&
        vehicle_gui_has_valid_route(&app->gui) &&
        !app->vehicle.has_entered_and_reported) {
        if (obu_app_publish_vehicle_state(app)) {
            app->pending_enter_publish = false;
            (void)vehicle_gui_consume_enter_request(&app->gui);
            app->gui.dirty = true;
            return;
        }
    }

    if (app->pending_leave_publish &&
        app->vehicle.has_entered_and_reported &&
        !app->vehicle.has_passed) {
        if (obu_app_publish_vehicle_pass(app)) {
            app->pending_leave_publish = false;
            (void)vehicle_gui_consume_leave_request(&app->gui);
            app->gui.dirty = true;
        }
    }
}

bool obu_app_handle_spat_message(
    ObuApp *app,
    const char *topic,
    const char *payload,
    size_t payload_len)
{
    SpatMessage message;

    (void)topic;

    if (app == NULL || payload == NULL) {
        return false;
    }

    if (!mqtt_client_parse_spat_json(payload, payload_len, &message)) {
        log_warn(kTag, "reject malformed SPaT");
        return false;
    }

    if (!spat_state_update(
            &app->spat,
            message.light_id,
            message.phase,
            message.remain_s,
            message.timestamp)) {
        log_warn(kTag, "cache SPaT failed");
        return false;
    }

    app->gui.dirty = true;
    log_info(
        kTag,
        "cached SPaT light=%s phase=%s remain=%d",
        app->spat.light_id,
        app->spat.phase,
        app->spat.remain_s);
    return true;
}

bool obu_app_debug_feed_spat(ObuApp *app, const char *topic, const char *payload)
{
    if (app == NULL || topic == NULL || payload == NULL) {
        return false;
    }

    return mqtt_client_on_message(&app->mqtt, topic, payload, strlen(payload));
}
