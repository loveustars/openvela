# Makefile for local testing (not for openvela build)
# This is for validating the C code on a PC before deploying to the board
#
# For OpenVela board build, use the build files in app/ directory:
#   app/Kconfig       - menuconfig 集成
#   app/Make.defs     - 源码注册
#   app/Makefile      - 应用级编译
#   app/CMakeLists.txt - CMake 编译

CC = gcc
CFLAGS = -Wall -Wextra -std=c99 -I./include -g
LDFLAGS =

# Source files: include local_main.c for PC binary (has main entry point)
SRCS = $(wildcard src/*.c)
# Exclude openvela-specific main and gui_main (both have their own main)
SRCS := $(filter-out src/traffic_controller_main.c src/gui_main.c,$(SRCS))

# For test binary, exclude local_main.c, traffic_controller_main.c, and gui_main.c
TEST_SRCS = $(filter-out src/local_main.c src/gui_main.c src/traffic_controller_main.c,$(SRCS))

# GUI server binary: exclude both mains and openvela main
GUI_SRCS = $(filter-out src/local_main.c src/traffic_controller_main.c,$(SRCS))

# OpenVela entry binary (for testing main.c on PC, no MQTT/NuttX)
VELA_SRCS = $(filter-out src/local_main.c src/gui_main.c,$(SRCS))

OBJS = $(SRCS:.c=.o)
TEST_OBJS = $(TEST_SRCS:.c=.o)
GUI_OBJS = $(GUI_SRCS:.c=.o)
VELA_OBJS = $(VELA_SRCS:.c=.o)

TARGET = traffic_ctrl_test
GUI_TARGET = gui_server
VELA_TARGET = traffic_ctrl_vela

.PHONY: all clean check run run-ns-rush run-demo help

all: $(TARGET) $(GUI_TARGET) $(VELA_TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(GUI_TARGET): $(GUI_OBJS) src/gui_main.o
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# OpenVela entry — test the board-side main without NuttX
$(VELA_TARGET): $(VELA_OBJS) src/traffic_controller_main.o
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# Pattern rules
src/%.o: src/%.c
	$(CC) $(CFLAGS) -c -o $@ $<

src/gui_main.o: src/gui_main.c
	$(CC) $(CFLAGS) -c -o $@ $<

src/traffic_controller_main.o: src/traffic_controller_main.c
	$(CC) $(CFLAGS) -c -o $@ $<

# Syntax check
check:
	@echo "Checking syntax..."
	@$(CC) $(CFLAGS) -fsyntax-only $(SRCS) 2>&1
	@echo "OK"

# Run simulations
run: $(TARGET)
	./$(TARGET) --scenario balanced --duration 30 --verbose

run-ns-rush: $(TARGET)
	./$(TARGET) --scenario ns_rush --duration 30 --verbose

run-left-rush: $(TARGET)
	./$(TARGET) --scenario left_rush --duration 30 --verbose

# Run test suite
test: $(TEST_OBJS)
	$(CC) $(CFLAGS) -o test_runner $(TEST_OBJS) tests/test_main.c $(LDFLAGS)
	./test_runner

run-demo: $(TARGET)
	./$(TARGET) --demo

# Run GUI server
gui: $(GUI_TARGET)
	./$(GUI_TARGET)

# Run board-style main (without NuttX/MQTT)
vela-test: $(VELA_TARGET)
	./$(VELA_TARGET) --sim

# Clean
clean:
	rm -f src/*.o $(TARGET) $(GUI_TARGET) $(VELA_TARGET) test_runner 2>/dev/null; true

# Help
help:
	@echo "Available targets:"
	@echo "  make all          - Build all binaries (PC + GUI + OpenVela entry)"
	@echo "  make check        - Syntax check only"
	@echo "  make run          - Run balanced scenario (30s)"
	@echo "  make run-ns-rush  - Run NS rush scenario"
	@echo "  make run-left-rush - Run left turn rush scenario"
	@echo "  make test         - Run test suite"
	@echo "  make run-demo     - Cycle through all scenarios"
	@echo "  make gui          - Build and run GUI server"
	@echo "  make vela-test    - Build board-style main (no NuttX)"
	@echo "  make clean        - Remove build artifacts"
	@echo ""
	@echo "For OpenVela board build, place the 'app/' directory into"
	@echo "apps/examples/traffic_controller/ and run menuconfig."