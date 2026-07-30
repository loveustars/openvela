# OpenVela 集成说明

本目录包含把控制器集成到 OpenVela 构建系统所需的所有文件。

## 文件清单（按教程要求 7 个文件）

| 文件 | 作用 |
|------|------|
| `Kconfig`         | menuconfig 配置项（搜索 `TRAFFIC_CONTROLLER`）|
| `Make.defs`       | 把本应用注册到 `CONFIGURED_APPS` |
| `Makefile`        | GNU Make 构建规则 |
| `CMakeLists.txt`  | CMake 构建规则 |
| `traffic_controller_main.c` | 应用入口（`int main(int argc, char *argv[])`）|
| `signal_controller.{c,h}` | 4 相位独立信号控制核心 |
| `simulator.{c,h}` | 仿真器（车辆到达规则）|
| `comm.{c,h}`      | MQTT 通信接口（与队友 A 交互）|

## 上板流程

```bash
# 1. 复制整个 app/ 目录到 OpenVela 源码树
cp -r app/ ~/vela-opensource/apps/examples/traffic_controller/

# 2. 在 apps/examples/Kconfig 末尾添加（如果还没有 source 该文件）：
#    source "traffic_controller/Kconfig"

# 3. 清理工程
cd ~/vela-opensource
./build.sh vendor/allwinnertech/boards/r528/r528s3-velaevb1/configs/nsh/ distclean

# 4. 配置：按 / 搜索 TRAFFIC_CONTROLLER，选中后按 Q 保存
./build.sh vendor/allwinnertech/boards/r528/r528s3-velaevb1/configs/nsh/ menuconfig

# 5. 编译 + 烧写
cd ~/vela-opensource/vendor/allwinnertech/lichee
m
pack

# 6. 串口运行
nsh> traffic_controller --sim           # 仿真模式（不连 MQTT）
nsh> traffic_controller                # 真实模式（连 MQTT 收 A 的数据）
```

## 配置项（menuconfig）

| 配置项 | 默认值 | 说明 |
|--------|--------|------|
| `EXAMPLES_TRAFFIC_CONTROLLER`            | n      | 主开关（y/m/n）|
| `EXAMPLES_TRAFFIC_CONTROLLER_PROGNAME`  | traffic_controller | 可执行文件名 |
| `EXAMPLES_TRAFFIC_CONTROLLER_PRIORITY`  | 100    | 任务优先级 |
| `EXAMPLES_TRAFFIC_CONTROLLER_STACKSIZE` | 16384  | 任务栈大小（字节）|
| `EXAMPLES_TRAFFIC_CONTROLLER_DEFAULT_SCENARIO` | balanced | 启动场景 |
| `EXAMPLES_TRAFFIC_CONTROLLER_MQTT_BROKER` | tcp://192.168.1.1:1883 | MQTT broker |
| `EXAMPLES_TRAFFIC_CONTROLLER_LOG_LEVEL`  | 1      | 日志详细度 |

## 板子命令

```bash
nsh> traffic_controller --help
用法: traffic_controller [选项]
选项:
  --scenario <name>  场景 (balanced/ns_rush/ew_rush/left_rush/alternating_wave/idle)
  --duration <sec>   仿真时长(秒)
  --sim               启用模拟模式(不依赖 MQTT)
  --demo              演示模式(循环播放各场景)
  --config <json>    JSON 格式配置参数

nsh> traffic_controller --sim
[t=0000s] NS_STRAIGHT dur=15 | initial startup
[t=0002s] NS_STRAIGHT dur=15 | NS straight cleared, switch to EW
...
```

## 与队友 A 的通信

- 订阅 topic: `v2i/vehicle/data` (JSON: `{"road":0,"lane":1,"position":10.5,"speed":0.0}`)
- 发布 topic: `v2i/spat/rsu_a` (JSON: `{"phase":"NS_STRAIGHT","elapsed":5,"mask":54}`)
- 默认 MQTT broker: `tcp://192.168.1.1:1883`

详见 `comm.h` / `comm.c`。
