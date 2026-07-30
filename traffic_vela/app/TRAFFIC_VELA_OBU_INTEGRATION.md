# traffic_vela 与 OBU 联调说明

本文档说明当前 `traffic_vela` 已按车端 `obu_vehicle_client` 协议对齐后的通信方式。

## 1. 联调拓扑

建议使用一台电脑同时承担：

1. 开热点
2. 运行 MQTT Broker

车端 OBU 和红绿灯端 `traffic_vela` 都连接到该热点，并连接同一个 Broker。

## 2. MQTT 配置

`traffic_vela` 运行时可传：

```text
--mqtt-host <broker_ip>
--mqtt-port 1883
```

示例：

```text
traffic_controller --mqtt-host 192.168.137.1 --mqtt-port 1883
```

## 3. 当前已对齐的 Topic

### 3.1 OBU -> RSU

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

### 3.2 RSU -> OBU

SPaT 下发：

```text
v2i/spat/obu_1
```

## 4. 当前已对齐的 JSON

### 4.1 ENTER

```json
{
  "vehicle_id": "obu_1",
  "from_direction": "E",
  "to_direction": "straight",
  "timestamp": 1710000001
}
```

### 4.2 LEAVE

```json
{
  "vehicle_id": "obu_1",
  "from_direction": "E",
  "to_direction": "straight",
  "status": "passed",
  "timestamp": 1710000008
}
```

### 4.3 SPaT

```json
{
  "light_id": "light_a",
  "phase": "green",
  "remain": 12,
  "timestamp": 1710000002
}
```

## 5. 红绿灯端内部逻辑

`traffic_vela` 现在按以下流程工作：

1. 订阅 OBU ENTER / LEAVE topic
2. 收到 ENTER 后：
   - 解析 `from_direction`
   - 解析 `to_direction`
   - 映射为 `road + lane`
   - 加入控制器队列
   - 记录当前 active vehicle session
3. 主循环每秒执行一次控制器决策
4. 根据 active vehicle 当前 movement 判断：
   - 当前允许通行 -> `green`
   - 黄灯阶段 -> `yellow`
   - 其余 -> `red`
5. 每秒向 `v2i/spat/obu_1` 发布一次 `remain`
6. 收到 LEAVE 后：
   - 从队列移除
   - 清除 active session
   - 停止继续向该车推送 SPaT

## 6. 现阶段联调能力边界

当前实现面向：

- 单车联调
- 单 OBU topic：`v2i/spat/obu_1`
- 车端默认同时向 `rsu_a` 和 `rsu_b` 发 ENTER / LEAVE

因此当前版本适合先完成：

1. 车端点击 ENTER
2. 红绿灯端收到
3. 红绿灯端每秒回 SPaT
4. 车端显示灯色和倒计时
5. 车端点击 LEAVE
6. 红绿灯端结束本轮会话

## 7. 运行建议

真实联调时：

```text
traffic_controller --mqtt-host <电脑热点IP> --mqtt-port 1883
```

不要再走：

- 旧的 `v2i/vehicle/data`
- 旧的 `v2i/spat/rsu_a`
- 旧的假 MQTT 打印逻辑

## 8. 期望日志

收到 ENTER：

```text
[RSU] recv enter vehicle_id=obu_1 from=E to=straight
```

推送 SPaT：

```text
[RSU] publish spat topic=v2i/spat/obu_1 phase=green remain=12
```

收到 LEAVE：

```text
[RSU] recv leave vehicle_id=obu_1 from=E to=straight
```
