############################################################################
# apps/examples/obu_vehicle_client/Makefile
############################################################################

include $(APPDIR)/Make.defs

PROGNAME  = $(CONFIG_EXAMPLES_OBU_VEHICLE_CLIENT_PROGNAME)
PRIORITY  = $(CONFIG_EXAMPLES_OBU_VEHICLE_CLIENT_PRIORITY)
STACKSIZE = $(CONFIG_EXAMPLES_OBU_VEHICLE_CLIENT_STACKSIZE)
MODULE    = $(CONFIG_EXAMPLES_OBU_VEHICLE_CLIENT)

MAINSRC = obu_main.c
CSRCS = \
    direction.c \
    json_builder.c \
    logging.c \
    mqtt_client.c \
    network_manager.c \
    obu_app.c \
    platform_adapter.c \
    syscall_compat.c \
    spat_state.c \
    topic_router.c \
    vehicle_gui.c \
    vehicle_state.c

CFLAGS += ${INCDIR_PREFIX}$(APPDIR)/graphics/lvgl
CFLAGS += ${INCDIR_PREFIX}$(APPDIR)/netutils/mqttc/MQTT-C/include
ifdef CONFIG_LV_USE_NUTTX_LIBUV
CFLAGS += ${INCDIR_PREFIX}$(APPDIR)/system/uv
endif

include $(APPDIR)/Application.mk
