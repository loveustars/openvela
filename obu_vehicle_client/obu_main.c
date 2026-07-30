#include <nuttx/config.h>

#include "config.h"
#include "logging.h"
#include "obu_app.h"
#include "platform_adapter.h"

int main(int argc, char *argv[])
{
    ObuApp app;

    (void)argc;
    (void)argv;

    if (!platform_adapter_init()) {
        log_error(OBU_APP_NAME, "platform init failed");
        return 1;
    }

    if (!obu_app_init(&app)) {
        log_error(OBU_APP_NAME, "app init failed");
        return 1;
    }

    log_info(OBU_APP_NAME, "app started");

    while (1) {
        unsigned long now_ms = platform_adapter_get_time_ms();
        obu_app_step(&app, now_ms);
        platform_adapter_sleep_ms(MAIN_LOOP_INTERVAL_MS);
    }

    return 0;
}
