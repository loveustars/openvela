# 智能交通信号控制器

基于 OpenVela RTOS 的 V2I 车路协同系统信号控制模块。

## 核心设计

### 三相位信号控制

路口采用三相位设计，解决对向车流冲突问题：

```
NS_ALL_GO  ──[黄灯 3s]──[全红 1s]──  LEFT_ONLY  ──[黄灯 3s]──[全红 1s]──  EW_ALL_GO
    ↑                                                                              │
    └──────────────────────────────────────────────────────────────────────────────┘
```

| 相位 | 放行 | 禁行 | 说明 |
|------|------|------|------|
| NS_ALL_GO | N/S 直行、右转 | N/S 左转 | 南北直行，左转等下一相位 |
| EW_ALL_GO | E/W 直行、右转 | E/W 左转 | 东西直行，左转等下一相位 |
| LEFT_ONLY | 全部左转 | 直行、右转 | 集中放行积压左转 |
| YELLOW | — | 全部 | 安全过渡 |
| ALL_RED | — | 全部 | 清空路口 |

### 右转始终允许

右转车辆在最右侧车道通行，不会与任何方向的左转或直行产生冲突，因此**右转在任何相位均可通行**。

这意味着：
- 信号灯不需要为右转单独设灯
- 右转车辆可以在红灯时直接右转（前提是路口安全）
- 控制器的相位决策不影响右转通行

## 车道掩码

通行许可用比特位掩码表示：

```
bit0 (0x01) = LEFT（左转）
bit1 (0x02) = STRAIGHT（直行）
bit2 (0x04) = RIGHT（右转，始终为 1）
```

| 相位 | 掩码 | 含义 |
|------|------|------|
| NS_ALL_GO | 0b110 | 直行+右转 |
| EW_ALL_GO | 0b110 | 直行+右转 |
| LEFT_ONLY | 0b101 | 左转+右转 |
| YELLOW | 0b100 | 仅右转 |
| ALL_RED | 0b100 | 仅右转 |

## 项目结构

```
smart_traffic_openvela/
├── include/
│   ├── signal_controller.h   # 核心控制器
│   ├── comm.h                 # 通信接口
│   └── simulator.h            # 模拟器
├── src/
│   ├── signal_controller.c
│   ├── comm.c
│   ├── simulator.c
│   ├── main.c                 # OpenVela 入口
│   └── local_main.c           # PC 测试入口
├── scripts/
│   └── simulator_data.py      # 模拟数据生成
├── tests/
│   └── test_main.c
├── Makefile
└── README.md
```

## 快速开始

### PC 端测试

```bash
cd smart_traffic_openvela
make all

# 运行仿真
./traffic_ctrl_test --scenario balanced --duration 30 --verbose

# 左转高峰场景
./traffic_ctrl_test --scenario left_rush --duration 60 --verbose

# 演示模式（循环所有场景）
./traffic_ctrl_test --demo

# 运行测试
make test
```

### 生成模拟数据

```bash
python3 scripts/simulator_data.py --scenario balanced --output data.json
python3 scripts/simulator_data.py --scenario left_rush --format compact
python3 scripts/simulator_data.py --stream --interval 1.0
```

## 场景说明

| 场景 | 说明 |
|------|------|
| balanced | 南北/东西均衡，三车道均匀分布 |
| ns_rush | 南北方向高峰期，左转需求较大 |
| ew_rush | 东西方向高峰期 |
| left_rush | 各方向左转需求明显 |
| alternating_wave | 前30秒NS高峰，后30秒EW高峰 |
| idle | 无车辆输入 |

## 算法核心

### 需求评分

```c
score = base_weight
      + waiting_bonus        // 等待中
      + stopped_queue_bonus // 完全停止
      + proximity_bonus * (1 - position / proximity_range) // 接近停止线
```

### 防饥饿机制

```c
effective_score = raw_score + waiting_seconds * starvation_weight
```

等待时间长的走廊会累积补偿分，确保不会长期得不到通行机会。

### 动态绿灯

```c
planned = base_green + round(score_delta_ratio * adjustment_window)
planned = clamp(planned, min_green, max_green)
```

根据两侧需求差动态调整绿灯时长，但不超过边界限制。

## 协作

### 输入格式

由队友 A 通过 MQTT 订阅后注入：

```json
{
  "vehicles": [
    {"vehicle_id": "N-L-001", "road": "N", "lane": "LEFT", "position": 15.0, "speed": 0.0, "is_waiting": true},
    {"vehicle_id": "N-S-002", "road": "N", "lane": "STRAIGHT", "position": 20.0, "speed": 2.0, "is_waiting": false}
  ]
}
```

### 输出格式（信号决策）

控制器发布到 MQTT Topic `v2i/spat/rsu_a`：

```json
{
  "phase": "NS_ALL_GO",
  "duration": 20,
  "phase_elapsed": 5,
  "allowed_lanes_mask": 6,
  "reason": "NS demand balanced with EW",
  "stats": {
    "road_lanes": {
      "N": {"LEFT": 2, "STRAIGHT": 3, "RIGHT": 1},
      "S": {"LEFT": 1, "STRAIGHT": 4, "RIGHT": 0}
    }
  }
}
```

## 紧急处理

- **通信中断**：超过5秒未收到心跳，进入安全模式（最小绿灯循环）

## 编译（OpenVela）

```bash
# 复制文件到 openvela 源码
cp -r include/* ~/vela-opensource/apps/include/
cp src/signal_controller.c src/comm.c src/simulator.c ~/vela-opensource/apps/smart_traffic/

# 编译
cd ~/vela-opensource/vendor/allwinnertech/lichee/
source vela_env.sh && source envsetup.sh
lunch_nuttx  # 选择 2
m && pack

# 运行
nsh> traffic_ctrl --scenario balanced --sim
```

## 典型输出

```
>>> 相位切换: ? → NS_ALL_GO <<<
[t=0000s] NS_ALL_GO  dur=20 elap= 0 (直行+右转通行) | initial startup

>>> 相位切换: NS_ALL_GO → YELLOW <<<
[t=0020s] YELLOW      dur= 3 elap= 0 | NS green max reached

>>> 相位切换: YELLOW → ALL_RED <<<
[t=0023s] ALL_RED     dur= 1 elap= 0 | clearance before LEFT_ONLY

>>> 相位切换: ALL_RED → LEFT_ONLY <<<
[t=0024s] LEFT_ONLY   dur= 8 elap= 0 (左转+右转通行) | checking left turn
  允许通行: 左转+右转
```

## 已知限制

1. 多路口协同暂未实现
2. 当前假设所有道路都是三车道配置

## 4 相位独立设计（v2.0.0）

### 设计动机

传统三相位（左转保护合并）会让左转被直行时间挤压。
4 相位独立设计参考 NEMA 双环结构，让左转和直行有**各自独立的绿灯时间**。

### 4 个主相位

```
NS_STRAIGHT ──[Y 3s]──[AR 1s]── NS_LEFT ──[Y 3s]──[AR 1s]──
  EW_STRAIGHT ──[Y 3s]──[AR 1s]── EW_LEFT ──[Y 3s]──[AR 1s]──
       ↑___________________________________________|
```

| 相位 | 放行动作 |
|------|----------|
| `NS_STRAIGHT` | N/S 直行 + N/S 右转 |
| `NS_LEFT`     | N/S 左转 |
| `EW_STRAIGHT` | E/W 直行 + E/W 右转 |
| `EW_LEFT`     | E/W 左转 |
| `YELLOW`      | （右转可在 YELLOW 阶段通行）|
| `ALL_RED`     | 全停（右转可在 ALL_RED 阶段通行）|

### 12-bit 通行许可

每个 `allowed_lanes_mask` 包含 12 个 bit：

```
bit  0:  N  左转      bit  6:  E  左转
bit  1:  N  直行      bit  7:  E  直行
bit  2:  N  右转      bit  8:  E  右转
bit  3:  S  左转      bit  9:  W  左转
bit  4:  S  直行      bit 10:  W  直行
bit  5:  S  右转      bit 11:  W  右转
```

例如 `EW_STRAIGHT` 阶段：`mask = PERMIT_E_S | PERMIT_E_R | PERMIT_W_S | PERMIT_W_R`。
YELLOW 阶段：`mask = RIGHT_MASK_ALL`（仅右转位被置位）。

### 4 个独立饥饿计数器

控制器跟踪每个相位自上次绿灯以来的 tick 数：

```c
ticks_since_ns_straight   // NS 直行多久没服务
ticks_since_ns_left       // NS 左转多久没服务
ticks_since_ew_straight   // EW 直行多久没服务
ticks_since_ew_left       // EW 左转多久没服务
```

任一计数器达到 30 tick 触发饥饿保护，强制切换到该相位。

### 4 板演示模式

只有 4 个板子当作车给红绿灯发信息。代码针对这种**极低样本**场景做了优化：

#### 关键 API

```c
// 板子发一帧数据后即可离开，控制器内部维护队列
controller_report_vehicle(ctrl, ROAD_N, LANE_STRAIGHT);
controller_report_vehicle(ctrl, ROAD_E, LANE_LEFT);

// 每 tick 调用此函数即可（自动构建车辆+决策+放行）
controller_step_internal(ctrl);
```

#### 演示场景下的表现

| 场景 | 用时 | 切换次数 |
|------|------|---------|
| 4 板同时发 4 方向各 1 辆 | 10 ticks | 3 |
| 4 板 0/5/10/15 tick 错开发 | 19 ticks | 3 |
| 12 方向各 1 辆同时发 | 15 ticks | 4 |
| 1 辆 N 直行（idle） | 1 tick | 0（早切）|

#### HTTP 板子端点

`make gui` 启动后，板子通过 HTTP 报告：

```bash
# 北方向直行车到达
curl -X POST http://<server>:8080/api/board/N_S

# 东方向左转车到达
curl -X POST http://<server>:8080/api/board/E_L

# 西方向右转车到达
curl -X POST http://<server>:8080/api/board/W_R
```

格式：`<road>_<lane>`，其中 `road ∈ {N, S, E, W}`，`lane ∈ {L, S, R}`。

返回：
```json
{"status":"ok","road":0,"lane":1,"queue":1}
```

### 智能行为（在极低样本下）

1. **1 辆车到达**：立即响应，智能早切
2. **1 辆右转**：右转在 YELLOW/ALL_RED 阶段可通行，0 tick 内通过
3. **空载**：max_green=45 强制轮转，所有 4 个相位都被服务
4. **饥饿保护**：任一相位 30 tick 未服务即强制切换
5. **直行+右转合并评分**：1 辆右转车也能触发 EW_STRAIGHT 相位