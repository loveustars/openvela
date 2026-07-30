# CMakeLists.txt for traffic_controller (OpenVela CMake build)
#
# 源码在 app/ 目录（自包含）

if(CONFIG_EXAMPLES_TRAFFIC_CONTROLLER)
  nuttx_add_application(
    NAME
      traffic_controller
    PRIORITY
      ${CONFIG_EXAMPLES_TRAFFIC_CONTROLLER_PRIORITY}
    STACKSIZE
      ${CONFIG_EXAMPLES_TRAFFIC_CONTROLLER_STACKSIZE}
    MODULE
      ${CONFIG_EXAMPLES_TRAFFIC_CONTROLLER}
    SRCS
      traffic_controller_main.c
      signal_controller.c
      comm.c
      simulator.c
      gui_lvgl.c
  )
endif()