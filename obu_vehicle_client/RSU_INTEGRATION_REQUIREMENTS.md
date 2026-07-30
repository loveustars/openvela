# 红绿灯 / RSU 对接统一需求

## 1. 目的

本文档用于统一车端 OBU 与红绿灯 / RSU 之间的通信对接要求，避免后续联调时 topic、字段、时序不一致。

## 2. 角色定义

### 2.1 OBU

车端设备，负责：

1. 用户选择行驶方向
2. 点击 `ENTER` 时上报进入路口
3. 接收红绿灯 SPaT
4. 实时显示当前灯色与剩余秒数
5. 点击 `LEAVE` 时上报已通过路口

### 2.2 RSU / 红绿灯控制端

路口侧设备，负责：

1. 接收车辆进入事件
2. 根据车辆关注的路口/灯组，返回当前灯色与剩余时间
3. 在车辆通行期间持续发送 SPaT
4. 接收车辆离开事件

## 3. 通信时序要求

## 3.1 进入阶段

1. 车辆点击 `ENTER`
2. OBU 立即发送进入消息
3. RSU 收到后必须立即回发当前 SPaT

要求：

- 从收到 `ENTER` 到首次返回 SPaT，建议不超过 `200ms`

## 3.2 通行阶段

RSU 必须持续发送 SPaT，而不是只发一次。

建议：

- 发送频率：`1Hz`
- 若倒计时每秒变化，则每秒发一条
- 若相位发生变化，立即补发一条

否则车端只能显示最后一次收到的 `remain`，不能准确显示实时倒计时。

## 3.3 离开阶段

1. 车辆点击 `LEAVE`
2. OBU 立即发送通过消息
3. RSU 收到后结束本车关联流程

RSU 可选择：

1. 停止针对该车的 SPaT 推送
2. 或继续按照公共路口广播策略发送

## 4. Topic 统一要求

## 4.1 OBU -> RSU

进入消息：

```text
v2i/vehicle/state/rsu_a
v2i/vehicle/state/rsu_b
```

离开消息：

```text
v2i/vehicle/pass/rsu_a
v2i/vehicle/pass/rsu_b
```

说明：

- 当前 OBU 默认同时向 `rsu_a`、`rsu_b` 发送
- 若后期改成按具体路口单发，需要双方重新确认 topic 规则

## 4.2 RSU -> OBU

SPaT 下发 topic：

```text
v2i/spat/obu_1
```

说明：

- 当前 OBU 订阅固定 topic
- 若一车一 topic，需要按 `vehicle_id` 派生

## 5. Payload 统一要求

## 5.1 进入消息格式

```json
{
  "vehicle_id": "obu_1",
  "from_direction": "E",
  "to_direction": "straight",
  "timestamp": 1710000001
}
```

字段要求：

- `vehicle_id`: string，车辆唯一标识
- `from_direction`: string，可选值 `E/S/W/N`
- `to_direction`: string，可选值 `left/straight/right`
- `timestamp`: uint，秒级 Unix 时间戳

## 5.2 离开消息格式

```json
{
  "vehicle_id": "obu_1",
  "from_direction": "E",
  "to_direction": "straight",
  "status": "passed",
  "timestamp": 1710000008
}
```

字段要求：

- `status` 固定为 `passed`

## 5.3 SPaT 消息格式

```json
{
  "light_id": "light_a",
  "phase": "green",
  "remain": 12,
  "timestamp": 1710000002
}
```

字段要求：

- `light_id`: string，灯组标识，不能为空
- `phase`: string，只接受：
  - `red`
  - `yellow`
  - `green`
- `remain`: int，单位秒，必须大于等于 0
- `timestamp`: uint，秒级 Unix 时间戳

## 6. RSU 侧必须满足的最小能力

RSU / 红绿灯控制端至少要满足：

1. 能订阅 OBU 进入消息
2. 能订阅 OBU 离开消息
3. 能按 OBU topic 回发 SPaT
4. 能持续更新 `remain`
5. `phase` 字段必须严格使用小写：
   - `red`
   - `yellow`
   - `green`

## 7. 异常处理要求

## 7.1 OBU 未收到首次 SPaT

如果 OBU 点击 `ENTER` 后未收到 SPaT：

- RSU 应检查是否正确订阅到：
  - `v2i/vehicle/state/rsu_a`
  - `v2i/vehicle/state/rsu_b`

## 7.2 SPaT 字段不合法

以下情况 OBU 会拒收：

1. `phase` 不是 `red/yellow/green`
2. `remain < 0`
3. payload 缺少关键字段

## 7.3 只发一次 SPaT

如果 RSU 只发一次 SPaT：

- GUI 只能显示最后一次倒计时值
- 无法保证持续准确倒计时

因此明确要求：

- RSU 必须持续发送 SPaT

## 8. 联调检查清单

RSU 联调前确认：

1. Broker 地址与端口正确
2. OBU 已成功连接 Wi-Fi
3. OBU 已成功连接 MQTT
4. OBU 已订阅 `v2i/spat/obu_1`
5. RSU 已订阅车辆进入/离开 topic
6. RSU 发送的 `phase` 为小写英文
7. RSU 持续发送 `remain`

## 9. 推荐日志

RSU 侧建议至少打印：

1. 收到 `ENTER`
2. 发送首次 SPaT
3. 每次 SPaT 更新
4. 收到 `LEAVE`

示例：

```text
[RSU] recv enter vehicle_id=obu_1 from=E to=straight
[RSU] publish spat topic=v2i/spat/obu_1 phase=green remain=12
[RSU] publish spat topic=v2i/spat/obu_1 phase=green remain=11
[RSU] recv leave vehicle_id=obu_1
```

## 10. 后续扩展建议

若后续要支持多车、多灯组、多路口，建议扩展：

1. `vehicle_id` 和 topic 的映射规则
2. `intersection_id`
3. `lane_id`
4. `phase_id`
5. `phase_end_timestamp`

当前版本先不强制要求，优先完成单车单路口稳定联调。
