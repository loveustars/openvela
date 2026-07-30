# OBU Vehicle Client 使用说明

## 1. 功能概述

当前版本实现以下功能：

1. 开机启动 `obu_vehicle_client`
2. `obu_vehicle_client` 启动后只检测 Wi-Fi 是否已连通
3. Wi-Fi 由用户在 `nsh` 手动连接
4. `obu_vehicle_client` 在 Wi-Fi 连通后连接 MQTT Broker
4. 车辆点击 `ENTER` 后，向 `rsu_a` 与 `rsu_b` 同时发送进入消息
5. 车辆订阅红绿灯下发的 SPaT 消息
6. 收到 SPaT 后，GUI 显示：
   - 灯色 `LIGHT`
   - 剩余秒数
7. 车辆点击 `LEAVE` 后，向 `rsu_a` 与 `rsu_b` 同时发送离开消息

补充：

- 当前版本已切回真实联调模式
- 需要连接 Wi-Fi 与 MQTT Broker 后才能完成车路通信

## 2. Wi-Fi 连接方式

当前版本不再自动读取 `/data/wifi.cfg` 发起连接。

原因：

- 避免开机后不断刷日志，影响 `nsh` 输入
- 方便在现场手动切换热点账号和密码

请在 `nsh` 手动输入：

```sh
ifup wlan0
wapi mode wlan0 2
wapi psk wlan0 <你的密码> 3 2
wapi essid wlan0 <你的热点名> 1
renew wlan0
wapi show wlan0
echo ready > /tmp/obu_wifi_ready
```

说明：

- 前 6 条命令用于手动连接 Wi-Fi
- 最后一条：

```sh
echo ready > /tmp/obu_wifi_ready
```

用于通知 `obu_vehicle_client`：

- Wi-Fi 已手动处理完成
- 现在可以开始 MQTT 连接

如果后续想重新手动连网，可先执行：

```sh
rm /tmp/obu_wifi_ready
```

## 3. MQTT 配置

当前支持两种方式：

1. 默认编译配置
2. 运行时配置文件覆盖

默认编译配置位于：

- [config.h](/home/ubuntu/vela-opensource/apps/examples/obu_vehicle_client/config.h:10)

默认参数：

```c
#define MQTT_CLIENT_ID "obu_1_client"
#define MQTT_SPAT_TOPIC "v2i/spat/#"
#define MQTT_BROKER_HOST "192.168.168.133"
#define MQTT_BROKER_PORT 1883
```

推荐联调时使用运行时配置文件：

```txt
/data/mqtt.cfg
```

格式如下：

```txt
HOST=192.168.137.1
PORT=1883
```

说明：

- 若 `/data/mqtt.cfg` 存在，则优先使用该文件
- 若不存在，则回退到 `config.h` 中的默认值
- 这样后续电脑热点 IP 变化时，不需要重新编译车端

## 4. 通信流程

### 4.1 开机流程

1. `rcS` 启动 `obu_vehicle_client &`
2. 用户在 `nsh` 手动连接 Wi-Fi
3. Wi-Fi 成功后，OBU 连接 MQTT Broker
4. 若存在 `/data/mqtt.cfg`，先读取 Broker 地址
5. OBU 订阅：

```text
v2i/spat/#
```

### 4.2 行车交互流程

1. 驾驶员在 GUI 选择：
   - `TO`
   - `FRO`
2. 点击 `ENTER`
3. OBU 发送进入消息到：

```text
v2i/vehicle/state/rsu_a
v2i/vehicle/state/rsu_b
```

4. 红绿灯/RSU 收到后，向：

```text
v2i/spat/obu_1
```

持续发送 SPaT

5. OBU 收到 SPaT 后实时刷新 GUI 上的：
   - `LIGHT`
   - 倒计时

6. 车辆离开时点击 `LEAVE`
7. OBU 发送离开消息到：

```text
v2i/vehicle/pass/rsu_a
v2i/vehicle/pass/rsu_b
```

## 5. 主题定义

### 5.1 OBU -> RSU

```text
v2i/vehicle/state/rsu_a
v2i/vehicle/state/rsu_b
v2i/vehicle/pass/rsu_a
v2i/vehicle/pass/rsu_b
```

### 5.2 RSU -> OBU

```text
v2i/spat/obu_1
```

## 6. 消息格式

### 6.1 ENTER 消息

```json
{
  "vehicle_id": "obu_1",
  "from_direction": "E",
  "to_direction": "straight",
  "status": "entering",
  "timestamp": 1710000001
}
```

### 6.2 LEAVE 消息

```json
{
  "vehicle_id": "obu_1",
  "from_direction": "E",
  "to_direction": "straight",
  "status": "passed",
  "timestamp": 1710000008
}
```

### 6.3 SPaT 消息

```json
{
  "light_id": "obu_1",
  "phase": "NS_STRAIGHT",
  "remain": 12,
  "timestamp": 1710000002
}
```

## 7. 红绿灯下发要求

为了让 GUI 上的倒计时保持准确，红绿灯端不要只发一次 SPaT。

建议：

1. `ENTER` 后立即回发当前 SPaT
2. 之后按 1Hz 持续发送 SPaT
3. 每次 `remain` 变化都重新发
4. 相位切换时立即发新消息

否则 GUI 只能显示最后一次收到的倒计时，不会自动精确递减。

## 8. 串口日志检查点

正常情况下应看到：

```text
[NET] wifi connected, MQTT can start now
[MQTT] connect ok host=... port=1883
[MQTT] subscribe topic=v2i/spat/#
```

点击 `ENTER` 后应看到：

```text
[MQTT] topic=v2i/vehicle/state/rsu_a
[MQTT] topic=v2i/vehicle/state/rsu_b
```

收到红绿灯消息后应看到：

```text
[MQTT] recv topic=v2i/spat/obu_1 bytes=...
[INFO][OBU] cached SPaT light=obu_1 phase=NS_STRAIGHT remain=12
```

点击 `LEAVE` 后应看到：

```text
[MQTT] topic=v2i/vehicle/pass/rsu_a
[MQTT] topic=v2i/vehicle/pass/rsu_b
```

## 9. 编译与打包

```bash
cd ~/vela-opensource/vendor/allwinnertech/lichee
source vela_env.sh
./build.sh vendor/allwinnertech/boards/r528/r528s3-velaevb1/configs/nsh/ -e -Wno-error -v -j32
pack
```

如 `pack` 需要环境：

```bash
source envsetup.sh
```

## 10. 当前限制

1. Wi-Fi 需要用户在 `nsh` 手动连接
2. 当前订阅 `v2i/spat/#`
3. 当前默认只向 `rsu_a` 发送进入/离开消息
4. 若红绿灯不持续发送 SPaT，倒计时不会长期精确
