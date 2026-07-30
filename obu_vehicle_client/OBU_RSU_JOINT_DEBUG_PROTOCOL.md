# OBU <-> RSU 联调协议说明

本文档给红绿灯 / RSU 对接方使用。

目标是让对方不看你整个工程，也能按统一协议完成联调。

## 1. 当前车端身份

- 车端程序名：`obu_vehicle_client`
- 车辆 ID：`obu_1`
- MQTT Client ID：`obu_1_client`
- 车端订阅 SPaT topic：`v2i/spat/obu_1`
- 当前默认同时向两个 RSU 发车端状态：
  - `rsu_a`
  - `rsu_b`

相关定义见 [config.h](/home/ubuntu/vela-opensource/apps/examples/obu_vehicle_client/config.h)。

## 2. 联调流程

### 2.1 车辆进入路口

用户在屏幕上选择：

- `FROM`: `E/S/W/N`
- `TO`: `left/straight/right`

点击 `ENTER` 后，车端立即发送进入消息。

发送 topic：

```text
v2i/vehicle/state/rsu_a
v2i/vehicle/state/rsu_b
```

### 2.2 RSU 返回灯态

RSU 收到 `ENTER` 后，应立即向车端返回当前灯态和剩余秒数。

返回 topic：

```text
v2i/spat/obu_1
```

### 2.3 通行期间持续推送

只发一次不够。

RSU 必须持续推送：

- 推荐频率：`1Hz`
- `remain` 每秒更新一次
- 相位变化时立即补发

### 2.4 车辆离开路口

用户点击 `LEAVE` 后，车端立即发送离开消息。

发送 topic：

```text
v2i/vehicle/pass/rsu_a
v2i/vehicle/pass/rsu_b
```

RSU 收到后，本次车灯联动流程结束。

## 3. JSON 协议

### 3.1 OBU -> RSU 进入消息

topic:

```text
v2i/vehicle/state/rsu_a
v2i/vehicle/state/rsu_b
```

payload:

```json
{
  "vehicle_id": "obu_1",
  "from_direction": "E",
  "to_direction": "straight",
  "timestamp": 1710000001
}
```

字段要求：

- `vehicle_id`: 固定为车端唯一标识
- `from_direction`: 只允许 `E/S/W/N`
- `to_direction`: 只允许 `left/straight/right`
- `timestamp`: Unix 秒级时间戳

### 3.2 OBU -> RSU 离开消息

topic:

```text
v2i/vehicle/pass/rsu_a
v2i/vehicle/pass/rsu_b
```

payload:

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

### 3.3 RSU -> OBU SPaT 消息

topic:

```text
v2i/spat/obu_1
```

payload:

```json
{
  "light_id": "light_a",
  "phase": "green",
  "remain": 12,
  "timestamp": 1710000002
}
```

字段要求：

- `light_id`: 非空字符串，必须带
- `phase`: 只允许小写
  - `red`
  - `yellow`
  - `green`
- `remain`: 大于等于 0 的整数，单位秒
- `timestamp`: Unix 秒级时间戳

## 4. 车端接收判定规则

车端会拒收以下 SPaT：

1. 没有 `phase`
2. `phase` 不是 `red/yellow/green`
3. 没有 `remain`
4. `remain < 0`
5. `light_id` 为空

也就是说，下面这种报文虽然看起来像对的，但车端不会按有效灯态处理：

```json
{
  "rsu_id": "rsu_a",
  "phase": "NS_STRAIGHT",
  "remain": 12
}
```

原因：

- `rsu_id` 不是车端当前要求的 `light_id`
- `NS_STRAIGHT` 不是车端支持的 `green/yellow/red`

## 5. RSU 侧最小实现要求

对方至少需要做到这些：

1. 真正连接 MQTT Broker
2. 真正订阅以下 topic：
   - `v2i/vehicle/state/rsu_a`
   - `v2i/vehicle/state/rsu_b`
   - `v2i/vehicle/pass/rsu_a`
   - `v2i/vehicle/pass/rsu_b`
3. 真正发布到：
   - `v2i/spat/obu_1`
4. 收到 `ENTER` 后立即返回首条 SPaT
5. 通行期间持续发送剩余秒数
6. 收到 `LEAVE` 后结束本次车辆跟踪

## 6. 红绿灯相位和车端灯色的映射建议

如果红绿灯控制器内部是更复杂的相位，比如：

- `NS_STRAIGHT`
- `NS_LEFT`
- `EW_STRAIGHT`
- `EW_LEFT`
- `YELLOW`
- `ALL_RED`

则给车端输出时，必须翻译成三态：

- 当前允许该车所选 movement 通行：`green`
- 黄灯过渡：`yellow`
- 其他情况：`red`

车端不会识别 `NS_STRAIGHT`、`EW_LEFT` 这类内部枚举。

## 7. 推荐联调日志

RSU 建议打印：

```text
[RSU] recv enter vehicle_id=obu_1 from=E to=straight
[RSU] publish spat topic=v2i/spat/obu_1 phase=green remain=12
[RSU] publish spat topic=v2i/spat/obu_1 phase=green remain=11
[RSU] recv leave vehicle_id=obu_1
```

车端建议观察：

```text
[MQTT] subscribe topic=v2i/spat/obu_1
[MQTT] topic=v2i/vehicle/state/rsu_a
[MQTT] topic=v2i/vehicle/state/rsu_b
[INFO][OBU] cached SPaT light=light_a phase=green remain=12
```

## 8. 你现在发过去的 `apps/examples/obu_vehicle_client` 是否够用

### 8.1 如果对方只是做协议联调

够用，但前提是他重点看这些文件：

- [OBU_RSU_JOINT_DEBUG_PROTOCOL.md](/home/ubuntu/vela-opensource/apps/examples/obu_vehicle_client/OBU_RSU_JOINT_DEBUG_PROTOCOL.md)
- [RSU_INTEGRATION_REQUIREMENTS.md](/home/ubuntu/vela-opensource/apps/examples/obu_vehicle_client/RSU_INTEGRATION_REQUIREMENTS.md)
- [OBU_WIFI_MQTT_USAGE.md](/home/ubuntu/vela-opensource/apps/examples/obu_vehicle_client/OBU_WIFI_MQTT_USAGE.md)
- [config.h](/home/ubuntu/vela-opensource/apps/examples/obu_vehicle_client/config.h)
- [obu_app.c](/home/ubuntu/vela-opensource/apps/examples/obu_vehicle_client/obu_app.c)
- [mqtt_client.c](/home/ubuntu/vela-opensource/apps/examples/obu_vehicle_client/mqtt_client.c)
- [json_builder.c](/home/ubuntu/vela-opensource/apps/examples/obu_vehicle_client/json_builder.c)
- [topic_router.c](/home/ubuntu/vela-opensource/apps/examples/obu_vehicle_client/topic_router.c)

### 8.2 如果对方想在自己环境里直接编这个 OBU

只发 `apps/examples/obu_vehicle_client` 不够。

因为它还依赖：

1. OpenVela / NuttX 整体工程
2. `apps/netutils/mqttc/MQTT-C/include` 头文件
3. 板级显示、触摸、网络设备环境
4. 你的板级启动配置和编译配置

也就是说：

- 用它理解协议：够
- 用它直接复现你板子上的完整 OBU 工程：不够

## 9. 如果还要补发，建议补哪些

如果你是发给“红绿灯通信对接方”，建议再补发这几样即可：

1. 本文档  
   - `apps/examples/obu_vehicle_client/OBU_RSU_JOINT_DEBUG_PROTOCOL.md`
2. 现有统一需求文档  
   - `apps/examples/obu_vehicle_client/RSU_INTEGRATION_REQUIREMENTS.md`
3. 车端 Wi-Fi / MQTT 使用文档  
   - `apps/examples/obu_vehicle_client/OBU_WIFI_MQTT_USAGE.md`
4. 样例报文目录  
   - `apps/examples/obu_vehicle_client/sample_data/`

如果你是发给“帮你一起编板子的人”，还要补发：

1. 板级启动脚本  
2. 相关 defconfig
3. 你实际使用的 build / pack 流程说明
4. 外围依赖关闭项说明

## 10. 最终结论

你现在已经发过去的 `apps/examples/obu_vehicle_client`：

- 对协议联调来说，基本够了
- 对完整复现你的板端工程来说，不够

对方如果只是改红绿灯通信模块，重点不该看 GUI，而该严格按本文档的：

- topic
- JSON 字段
- phase 取值
- ENTER / SPaT / LEAVE 时序

来做。
