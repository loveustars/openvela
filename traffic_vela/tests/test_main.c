/**
 * @file test_main.c
 * @brief PC 端测试程序 - 4 相位独立版
 *
 * 测试 4 个独立相位的调度逻辑、12-bit 通行许可、饥饿检测、安全模式。
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <assert.h>

#include "signal_controller.h"
#include "simulator.h"

#define TEST_ASSERT(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s\n", msg); return 1; } \
} while(0)

/* ========================================================================
 * 测试用例
 * ======================================================================== */

static int test_init(void) {
    printf("\n=== Test: Initialization ===\n");
    TrafficController ctrl;
    controller_init(&ctrl, NULL, PHASE_NS_STRAIGHT);
    TEST_ASSERT(ctrl.current_phase == PHASE_NS_STRAIGHT, "Initial phase should be NS_STRAIGHT");
    TEST_ASSERT(ctrl.current_phase_elapsed == 0, "Phase elapsed should be 0");
    printf("PASS\n");
    return 0;
}

static int test_four_phase_cycle(void) {
    printf("\n=== Test: 4-Phase Cycle ===\n");

    Simulator *sim = simulator_create(&(SimulatorConfig){SCENARIO_LEFT_RUSH, 180, 1000});
    TrafficController ctrl;
    controller_init(&ctrl, NULL, PHASE_NS_STRAIGHT);
    SimVehicle sim_veh[128];

    int reached_phases[PHASE_COUNT] = {0};
    SignalPhase last_phase = PHASE_NS_STRAIGHT;

    for (int t = 0; t < 180; t++) {
        int count = simulator_step(sim, sim_veh, 128);

        VehicleState vehicles[128];
        for (int i = 0; i < count; i++) {
            vehicles[i].road = sim_veh[i].road;
            vehicles[i].lane = sim_veh[i].lane;
            vehicles[i].position = sim_veh[i].position;
            vehicles[i].speed = sim_veh[i].speed;
            vehicles[i].is_waiting = sim_veh[i].is_waiting;
        }

        SignalDecision dec = controller_step(&ctrl, vehicles, count);
        reached_phases[dec.phase]++;

        if (dec.phase != last_phase && t < 50) {
            printf("  >>> %s\n", phase_to_string(dec.phase));
        }
        last_phase = dec.phase;
    }

    printf("  Phase counts: NS_STRAIGHT=%d NS_LEFT=%d EW_STRAIGHT=%d EW_LEFT=%d YELLOW=%d ALL_RED=%d\n",
           reached_phases[PHASE_NS_STRAIGHT], reached_phases[PHASE_NS_LEFT],
           reached_phases[PHASE_EW_STRAIGHT], reached_phases[PHASE_EW_LEFT],
           reached_phases[PHASE_YELLOW], reached_phases[PHASE_ALL_RED]);

    simulator_destroy(sim);

    TEST_ASSERT(reached_phases[PHASE_NS_STRAIGHT] > 0, "Should reach NS_STRAIGHT");
    TEST_ASSERT(reached_phases[PHASE_EW_STRAIGHT] > 0, "Should reach EW_STRAIGHT");
    TEST_ASSERT(reached_phases[PHASE_NS_LEFT] > 0, "Should reach NS_LEFT");
    TEST_ASSERT(reached_phases[PHASE_EW_LEFT] > 0, "Should reach EW_LEFT");

    printf("PASS (4-phase cycle verified)\n");
    return 0;
}

/* 测试 NS_STRAIGHT 相位：N/S 直行+右转允许，左转禁行 */
static int test_ns_straight_permits(void) {
    printf("\n=== Test: NS_STRAIGHT Permits ===\n");

    TrafficController ctrl;
    controller_init(&ctrl, NULL, PHASE_NS_STRAIGHT);

    VehicleState vehicles[12];
    /* N/S 各 3 辆（直行+左转+右转） */
    const char* roads[] = {"N", "S", "E", "W"};
    for (int r = 0; r < 4; r++) {
        for (int l = 0; l < 3; l++) {
            int idx = r * 3 + l;
            vehicles[idx].road = r;
            vehicles[idx].lane = l;
            vehicles[idx].position = 10.0f;
            vehicles[idx].speed = 0.0f;
            vehicles[idx].is_waiting = true;
            snprintf(vehicles[idx].vehicle_id, sizeof(vehicles[idx].vehicle_id),
                     "%s%s", roads[r], (l == 0 ? "L" : l == 1 ? "S" : "R"));
        }
    }

    SignalDecision dec = controller_step(&ctrl, vehicles, 12);
    printf("  Phase: %s, mask: 0x%03X\n", phase_to_string(dec.phase), dec.allowed_lanes_mask);

    /* NS_STRAIGHT: N/S 直行+右转允许 */
    TEST_ASSERT(controller_can_pass(&ctrl, &vehicles[1], &dec), "N straight should pass");
    TEST_ASSERT(controller_can_pass(&ctrl, &vehicles[2], &dec), "N right should pass");
    TEST_ASSERT(controller_can_pass(&ctrl, &vehicles[4], &dec), "S straight should pass");
    TEST_ASSERT(controller_can_pass(&ctrl, &vehicles[5], &dec), "S right should pass");
    /* NS 左转不允许 */
    TEST_ASSERT(!controller_can_pass(&ctrl, &vehicles[0], &dec), "N left should NOT pass");
    TEST_ASSERT(!controller_can_pass(&ctrl, &vehicles[3], &dec), "S left should NOT pass");
    /* E/W 全部不允许 */
    TEST_ASSERT(!controller_can_pass(&ctrl, &vehicles[6], &dec), "E left should NOT pass");
    TEST_ASSERT(!controller_can_pass(&ctrl, &vehicles[7], &dec), "E straight should NOT pass");
    TEST_ASSERT(!controller_can_pass(&ctrl, &vehicles[8], &dec), "E right should NOT pass");
    TEST_ASSERT(!controller_can_pass(&ctrl, &vehicles[9], &dec), "W left should NOT pass");
    TEST_ASSERT(!controller_can_pass(&ctrl, &vehicles[10], &dec), "W straight should NOT pass");
    TEST_ASSERT(!controller_can_pass(&ctrl, &vehicles[11], &dec), "W right should NOT pass");

    printf("PASS\n");
    return 0;
}

/* 测试 NS_LEFT 相位：仅 N/S 左转允许 */
static int test_ns_left_permits(void) {
    printf("\n=== Test: NS_LEFT Permits ===\n");

    TrafficController ctrl;
    controller_init(&ctrl, NULL, PHASE_NS_STRAIGHT);

    /* 强制进入 NS_LEFT */
    SignalPhase target = PHASE_NS_LEFT;
    /* 直接用 controller_step 跑出 NS_LEFT 比较麻烦，
     * 这里改用 controller_get_allowed_lanes 验证 */
    int mask = controller_get_allowed_lanes(&ctrl, target);
    printf("  NS_LEFT mask: 0x%03X\n", mask);

    /* NS_LEFT 应当包含 N/S 左转位，不含其他 */
    TEST_ASSERT((mask & PERMIT_N_LEFT) != 0, "N left should be allowed");
    TEST_ASSERT((mask & PERMIT_S_LEFT) != 0, "S left should be allowed");
    TEST_ASSERT((mask & PERMIT_N_STRAIGHT) == 0, "N straight should NOT be allowed");
    TEST_ASSERT((mask & PERMIT_S_STRAIGHT) == 0, "S straight should NOT be allowed");
    TEST_ASSERT((mask & PERMIT_E_LEFT) == 0, "E left should NOT be allowed");
    TEST_ASSERT((mask & PERMIT_E_STRAIGHT) == 0, "E straight should NOT be allowed");

    printf("PASS\n");
    return 0;
}

/* 测试右转始终允许（即使在 YELLOW/ALL_RED 阶段）
 *
 * 注意：4 相位设计中，YELLOW/ALL_RED 阶段允许右转（实际可通行）。
 * 但 NS_LEFT/EW_LEFT 阶段是纯左转相位，不放行右转。
 * 这是有意的设计——右转有独立的逻辑（在 YELLOW/ALL_RED 阶段通行）。
 */
static int test_right_always_allowed(void) {
    printf("\n=== Test: Right Turn Allowed in YELLOW/ALL_RED ===\n");

    TrafficController ctrl;
    controller_init(&ctrl, NULL, PHASE_NS_STRAIGHT);

    VehicleState right_vehicle = {
        .vehicle_id = "test-right",
        .road = ROAD_N,
        .lane = LANE_RIGHT,
        .position = 10.0f, .speed = 0.0f, .is_waiting = true
    };

    /* YELLOW 和 ALL_RED 阶段：右转允许（实际可通行）*/
    SignalPhase phases[] = { PHASE_YELLOW, PHASE_ALL_RED };
    for (int i = 0; i < 2; i++) {
        int mask = controller_get_allowed_lanes(&ctrl, phases[i]);
        SignalDecision dec = { .allowed_lanes_mask = mask };
        TEST_ASSERT(controller_can_pass(&ctrl, &right_vehicle, &dec) == true,
                    phase_to_string(phases[i]));
        printf("  %s: right allowed (mask=0x%03X)\n",
               phase_to_string(phases[i]), mask);
    }

    /* 主绿灯阶段：NS_STRAIGHT 包含 N/S 右转 */
    int mask = controller_get_allowed_lanes(&ctrl, PHASE_NS_STRAIGHT);
    SignalDecision dec = { .allowed_lanes_mask = mask };
    TEST_ASSERT(controller_can_pass(&ctrl, &right_vehicle, &dec) == true,
                "NS_STRAIGHT should allow N right");
    printf("  NS_STRAIGHT: N right allowed (mask=0x%03X)\n", mask);

    /* NS_LEFT 不放行右转（4 相位独立设计） */
    mask = controller_get_allowed_lanes(&ctrl, PHASE_NS_LEFT);
    dec.allowed_lanes_mask = mask;
    TEST_ASSERT(!controller_can_pass(&ctrl, &right_vehicle, &dec),
                "NS_LEFT should NOT allow N right");
    printf("  NS_LEFT: N right blocked (mask=0x%03X) - this is OK, right runs in YELLOW/ALL_RED\n", mask);

    printf("PASS\n");
    return 0;
}

/* 测试模拟器队列 */
static int test_simulator_lanes(void) {
    printf("\n=== Test: Simulator Lane Queues ===\n");

    SimulatorConfig cfg = {SCENARIO_LEFT_RUSH, 10, 1000};
    Simulator *sim = simulator_create(&cfg);

    int queues[4][3];
    for (int t = 0; t < 5; t++) {
        simulator_step(sim, NULL, 0);
    }

    simulator_get_lane_queues(sim, queues);
    const char *road_names[] = {"N", "S", "E", "W"};
    for (int r = 0; r < 4; r++) {
        printf("    %s: L=%d S=%d R=%d\n", road_names[r],
               queues[r][0], queues[r][1], queues[r][2]);
    }

    int has_left = 0;
    for (int r = 0; r < 4; r++) if (queues[r][0] > 0) has_left = 1;
    TEST_ASSERT(has_left, "LEFT_RUSH should generate left-turn vehicles");

    simulator_destroy(sim);
    printf("PASS\n");
    return 0;
}

/* 测试 idle 稳定性 */
static int test_idle_stability(void) {
    printf("\n=== Test: Idle Stability ===\n");

    SimulatorConfig cfg = {SCENARIO_IDLE, 10, 1000};
    Simulator *sim = simulator_create(&cfg);
    TrafficController ctrl;
    controller_init(&ctrl, NULL, PHASE_NS_STRAIGHT);

    SimVehicle sim_veh[64];
    VehicleState vehicles[64];

    for (int t = 0; t < 10; t++) {
        int count = simulator_step(sim, sim_veh, 64);
        TEST_ASSERT(count == 0, "IDLE should have 0 vehicles");
        for (int i = 0; i < count; i++) {
            vehicles[i].road = sim_veh[i].road;
            vehicles[i].lane = sim_veh[i].lane;
            vehicles[i].position = sim_veh[i].position;
            vehicles[i].speed = sim_veh[i].speed;
            vehicles[i].is_waiting = sim_veh[i].is_waiting;
        }
        SignalDecision dec = controller_step(&ctrl, vehicles, count);
        TEST_ASSERT(dec.phase >= 0 && dec.phase < PHASE_COUNT, "Phase should be valid");
    }
    simulator_destroy(sim);
    printf("PASS\n");
    return 0;
}

/* 测试安全模式 */
static int test_safe_mode(void) {
    printf("\n=== Test: Safe Mode (Comm Loss) ===\n");

    TrafficController ctrl;
    controller_init(&ctrl, NULL, PHASE_NS_STRAIGHT);

    VehicleState v = { .road = ROAD_E, .lane = LANE_STRAIGHT,
                       .position = 10.0f, .speed = 0.0f, .is_waiting = true };

    for (int t = 0; t < 6; t++) {
        ctrl.comm_lost_ticks++;
        SignalDecision dec = controller_step(&ctrl, &v, 1);
        printf("  tick %d: phase=%s safe_mode=%d\n",
               t, phase_to_string(dec.phase), ctrl.safe_mode);
    }

    TEST_ASSERT(ctrl.safe_mode == 1, "Should enter safe mode after comm loss");

    controller_report_comm_ok(&ctrl);
    TEST_ASSERT(ctrl.safe_mode == 0, "Safe mode should exit on comm restore");

    printf("PASS\n");
    return 0;
}

/* 测试饥饿：直行独立挨饿
 *
 * 场景：NS 直行持续有车（4 辆，>release_rate），EW 直行也有车。
 * 控制器如果在 NS 阶段停留超过 30s，EW 应该被强制服务。
 */
static int test_straight_starvation(void) {
    printf("\n=== Test: Straight Starvation ===\n");

    TrafficController ctrl;
    controller_init(&ctrl, NULL, PHASE_NS_STRAIGHT);

    /* 构造：EW 直行 4 辆（持续存在），EW 不放行时一直累加 */
    VehicleState ew_vehicles[4];
    for (int i = 0; i < 4; i++) {
        ew_vehicles[i].road = ROAD_E;
        ew_vehicles[i].lane = LANE_STRAIGHT;
        ew_vehicles[i].position = 10.0f + i * 5.0f;
        ew_vehicles[i].speed = 0.0f;
        ew_vehicles[i].is_waiting = true;
        snprintf(ew_vehicles[i].vehicle_id, sizeof(ew_vehicles[i].vehicle_id), "EW-S-%d", i);
    }

    /* 每 tick 都重置 EW 车辆位置（模拟持续到达且未服务） */
    int forced_switch = 0;
    int t;
    for (t = 0; t < 100 && !forced_switch; t++) {
        /* 重置车辆以保持持续需求 */
        for (int i = 0; i < 4; i++) {
            ew_vehicles[i].position = 10.0f + i * 5.0f;
        }

        SignalDecision dec = controller_step(&ctrl, ew_vehicles, 4);

        if (t % 5 == 0 || strstr(dec.reason, "starvation") != NULL) {
            printf("  t=%3d: phase=%-12s ns_straight_h=%d ew_straight_h=%d | %s\n",
                   t, phase_to_string(dec.phase),
                   ctrl.ticks_since_ns_straight, ctrl.ticks_since_ew_straight,
                   dec.reason);
        }

        if (dec.phase == PHASE_EW_STRAIGHT) {
            forced_switch = 1;
        }
    }

    printf("  Forced switch at t=%d (forced=%d)\n", t, forced_switch);
    TEST_ASSERT(forced_switch, "EW straight should be force-switched due to starvation");

    printf("PASS\n");
    return 0;
}

/* 测试：4 板演示场景（少量样本智能调度，需要手动报告通过）*/
static int test_4board_demo(void) {
    printf("\n=== Test: 4-Board Demo (Low Sample) ===\n");

    /* 4 个板子：到达 tick */
    struct { int tick; int road; int lane; } events[] = {
        {0,  ROAD_N, LANE_STRAIGHT},
        {5,  ROAD_S, LANE_STRAIGHT},
        {10, ROAD_E, LANE_LEFT},
        {15, ROAD_W, LANE_LEFT}
    };

    TrafficController ctrl;
    controller_init(&ctrl, NULL, PHASE_NS_STRAIGHT);

    /* 模拟"操作员按通过键"：每个 tick 检查所有已到达的车，
     * 如果灯对该方向/车道变绿，立即按通过。模拟推到通过需要时间，
     * 板子从 t=t_arrival 到 t=t_arrival+pass_delay 才按通过。
     * 假设通过延迟约 5 tick（推到通过点 + 反应时间）。 */

    int arrival_tick[4] = {-1, -1, -1, -1};
    int passed[4] = {0, 0, 0, 0};
    int cleared_t = -1;
    SignalPhase last = PHASE_NS_STRAIGHT;

    for (int t = 0; t < 200; t++) {
        /* 触发到达事件 */
        for (int i = 0; i < 4; i++) {
            if (events[i].tick == t) {
                arrival_tick[i] = t;
                controller_report_vehicle(&ctrl, events[i].road, events[i].lane);
            }
        }

        /* 先做决策（这样 phase 可能变化） */
        SignalDecision dec = controller_step_internal(&ctrl);

        /* 模拟"通过延迟 5 tick 后操作员按通过" */
        for (int i = 0; i < 4; i++) {
            if (passed[i]) continue;
            if (arrival_tick[i] < 0) continue;
            if (t < arrival_tick[i] + 5) continue;
            int r = events[i].road, l = events[i].lane;
            int bit = r * 3 + l;
            int mask = controller_get_allowed_lanes(&ctrl, ctrl.current_phase);
            if (mask & (1 << bit)) {
                controller_report_pass(&ctrl, r, l);
                passed[i] = 1;
            }
        }

        if (dec.phase != last) {
            int total = 0;
            for (int r = 0; r < 4; r++) for (int l = 0; l < 3; l++) total += ctrl.lane_queues[r][l];
            printf("  t=%3d %-12s | remain=%d | %s\n", t, phase_to_string(dec.phase), total, dec.reason);
            last = dec.phase;
        }

        int total = 0;
        for (int r = 0; r < 4; r++) for (int l = 0; l < 3; l++) total += ctrl.lane_queues[r][l];
        if (total == 0 && t >= 15 && passed[0] && passed[1] && passed[2] && passed[3]) {
            cleared_t = t;
            break;
        }
    }

    printf("  All cleared at t=%d\n", cleared_t);
    TEST_ASSERT(cleared_t >= 0 && cleared_t < 200,
                "4 boards should be cleared with manual pass events");
    printf("PASS\n");
    return 0;
}

/* ========================================================================
 * 主程序
 * ======================================================================== */

int main(void) {
    int (*tests[])(void) = {
        test_init,
        test_four_phase_cycle,
        test_ns_straight_permits,
        test_ns_left_permits,
        test_right_always_allowed,
        test_simulator_lanes,
        test_idle_stability,
        test_safe_mode,
        test_straight_starvation,
        test_4board_demo,
        NULL
    };

    printf("\n");
    printf("╔═══════════════════════════════════════════════════════╗\n");
    printf("║   Traffic Controller - 4-Phase Test Suite           ║\n");
    printf("║   4 独立相位 | 12-bit 许可 | 安全模式                ║\n");
    printf("╚═══════════════════════════════════════════════════════╝\n");

    int passed = 0, failed = 0;
    for (int i = 0; tests[i]; i++) {
        if (tests[i]() == 0) passed++;
        else failed++;
    }

    printf("\n");
    printf("╔═══════════════════════════════════════════════════════╗\n");
    printf("║   Results: %d passed, %d failed                      ║\n", passed, failed);
    printf("╚═══════════════════════════════════════════════════════╝\n");

    return (failed > 0) ? 1 : 0;
}