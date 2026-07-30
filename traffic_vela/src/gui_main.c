/**
 * @file gui_main.c
 * @brief GUI 服务器入口
 */

#include "gui_server.h"
#include <stdio.h>

int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;

    ServerConfig config = {
        .port = 8080,
        .tick_interval_ms = 500,
        .max_clients = 8
    };

    printf("Traffic Controller GUI Server v1.2.0\n");
    printf("=====================================\n");
    printf("Listening on http://localhost:%d\n", config.port);
    printf("Press Ctrl+C to stop\n\n");

    return gui_server_start(&config);
}