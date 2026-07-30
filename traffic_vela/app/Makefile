# Makefile for traffic_controller (OpenVela app)
#
# 源码在 app/ 目录中（自包含）
# 参考 apps/examples/hello/Makefile

include $(APPDIR)/Make.defs

PROGNAME  = $(CONFIG_EXAMPLES_TRAFFIC_CONTROLLER_PROGNAME)
PRIORITY  = $(CONFIG_EXAMPLES_TRAFFIC_CONTROLLER_PRIORITY)
STACKSIZE = $(CONFIG_EXAMPLES_TRAFFIC_CONTROLLER_STACKSIZE)
MODULE    = $(CONFIG_EXAMPLES_TRAFFIC_CONTROLLER)

MAINSRC = traffic_controller_main.c
CSRCS   = signal_controller.c
CSRCS  += comm.c
CSRCS  += simulator.c
CSRCS  += gui_lvgl.c

CFLAGS += ${INCDIR_PREFIX}$(APPDIR)/netutils/mqttc/MQTT-C/include

include $(APPDIR)/Application.mk
