# OpenVela 智能交通控制系统

嵌入式 V2X 仿真系统：信号机服务端 + 车载 OBU 客户端，基于全志 T113-S3 + NuttX RTOS。

## 项目组成

| 端 | 路径 | 角色 |
|---|---|---|
| 信号机服务端 | `第9组-交通控制系统/0.5/traffic_vela/` | 路口信号控制，4 相位 + NEMA 双环 |
| 车载 OBU 客户端 | `第9组-交通控制系统/0.5/obu_vehicle/` | 车辆状态上报 + 订阅 SPaT |

## 系统架构

```
Signal Controller (traffic_vela)        OBU Vehicle (obu_vehicle)
         │                                       │
         │  MQTT publish (topic: v2x/spat)       │
         │ ───────────────────────────────────▶   │
         │                                       │
         │  MQTT publish (topic: v2x/vehicle)     │
         │ ◀───────────────────────────────────   │
         │                                       │
         ├─ 4 相位状态机 (NS/EW 直行+左转)         ├─ 车辆状态机
         ├─ NEMA 双环切换规则                       ├─ JSON 消息打包
         ├─ LVGL GUI (320x480 屏)                  ├─ LVGL 车载 HMI
         └─ 12-bit 通行许可位掩码                   └─ MQTT 自动重连
```

## 技术栈

- 编程语言：C99
- RTOS：NuttX
- 平台：OpenVela（基于全志 T113-S3）
- 通信：MQTT
- GUI：LVGL
- 多线程：pthread
- 序列化：手写 JSON（无第三方库）

## 代码结构

```
第9组-交通控制系统/
└── 0.5/
    ├── traffic_vela/                # 服务端（路口信号机）
    │   └── traffic_vela/
    │       ├── app/                  # 应用入口与配置
    │       │   ├── traffic_controller_main.c
    │       │   ├── signal_controller.c    # 4 相位 + NEMA 双环
    │       │   ├── signal_controller.h
    │       │   ├── comm.c / comm.h       # 通信抽象层
    │       │   ├── simulator.c / simulator.h
    │       │   └── gui_lvgl.c / gui_lvgl.h
    │       ├── src/                  # 核心实现
    │       ├── include/
    │       ├── tests/                # 单元测试
    │       └── gui/                  # 网页版 GUI
    │
    └── obu_vehicle/                 # 客户端（车载 OBU）
        └── home/ubuntu/vela-opensource/apps/examples/obu_vehicle_client/
            ├── obu_main.c
            ├── obu_app.c / obu_app.h
            ├── vehicle_state.c / vehicle_state.h
            ├── mqtt_client.c / mqtt_client.h
            ├── network_manager.c / network_manager.h
            ├── topic_router.c / topic_router.h
            ├── json_builder.c / json_builder.h
            ├── spat_state.c / spat_state.h
            ├── vehicle_gui.c / vehicle_gui.h
            ├── direction.c / direction.h
            ├── platform_adapter.c / platform_adapter.h
            ├── logging.c / logging.h
            └── config.h
```

## 核心算法：4 相位 NEMA 双环信号控制

信号机服务端用状态机实现 4 相位独立控制：

- NS_STRAIGHT：南北直行 + 右转
- NS_LEFT：南北左转
- EW_STRAIGHT：东西直行 + 右转
- EW_LEFT：东西左转

切换规则：

- NEMA 双环结构：环 A（NS_STRAIGHT ↔ NS_LEFT）、环 B（EW_STRAIGHT ↔ EW_LEFT），环内可自由切换，环间可交错
- 强制轮转：达到 max_green 必须切到 YELLOW
- 智能早切：当前方向清空 + 对向/本向其他相位有需求
- 防饥饿：硬阈值 30 tick 没被服务 → 切
- 安全模式：通信中断锁定 NS_STRAIGHT，不切换

12 个独立车道（4 方向 × 3 车道）通过 12-bit 通行许可位掩码控制：

```
bit 0:  N  左转     bit 6:  E  左转
bit 1:  N  直行     bit 7:  E  直行
bit 2:  N  右转     bit 8:  E  右转
bit 3:  S  左转     bit 9:  W  左转
bit 4:  S  直行     bit 10: W  直行
bit 5:  S  右转     bit 11: W  右转
```

## 构建与运行

见 [第9组-交通控制系统/0.5/traffic_vela/traffic_vela/SETUP_GUIDE.md](第9组-交通控制系统/0.5/traffic_vela/traffic_vela/SETUP_GUIDE.md)

环境：

- 主机：Ubuntu amd64（VMware 虚拟机）
- 目标板：全志 T113-S3 + OpenVela
- 通信 broker：本地 MQTT

## 团队与课程背景

- 课程：嵌入式系统（选修）
- 时间：2026.05 - 2026.07
- 组别：第 9 组
- 团队分工：本仓库服务端由本人主控，OBU 端为队友负责

## License

课程作业，未开源授权。
