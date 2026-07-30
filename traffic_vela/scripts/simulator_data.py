#!/usr/bin/env python3
"""
交通模拟数据生成器 - 三车道版

生成各种场景的模拟数据，用于开发测试和演示。

场景说明：
- balanced:        南北/东西均衡，三车道均匀分布
- ns_rush:         南北方向高峰期，左转需求较大
- ew_rush:         东西方向高峰期
- left_rush:       各方向左转需求明显
- alternating_wave: 前30秒NS高峰，后30秒EW高峰
- idle:            无车辆输入

Usage:
    python simulator_data.py --scenario balanced
    python simulator_data.py --scenario left_rush --output data.json
    python simulator_data.py --stream --interval 1.0
"""

from __future__ import annotations
import argparse
import json
import sys
import time
from dataclasses import dataclass, asdict
from typing import Optional

ROADS = ["N", "S", "E", "W"]
LANES = ["LEFT", "STRAIGHT", "RIGHT"]


@dataclass
class VehicleState:
    """单车状态"""
    vehicle_id: str
    road: str
    lane: str        # LEFT / STRAIGHT / RIGHT
    position: float
    speed: float
    is_waiting: bool

    def to_json(self) -> dict:
        return asdict(self)


@dataclass
class TickData:
    """每个时间片数据"""
    second: int
    vehicles: list[VehicleState]
    queues: dict       # {road: {lane: count}}

    def to_json(self) -> dict:
        return {
            "second": self.second,
            "vehicles": [v.to_json() for v in self.vehicles],
            "queues": self.queues
        }


class TrafficSimulator:
    """三车道交通模拟器"""

    def __init__(self, scenario: str = "balanced", duration: int = 120):
        self.scenario = scenario
        self.duration = duration
        self.lane_queues = {r: {l: 0 for l in LANES} for r in ROADS}
        self.vehicle_seq = 0
        self.current_tick = 0

    def reset(self):
        self.lane_queues = {r: {l: 0 for l in LANES} for r in ROADS}
        self.vehicle_seq = 0
        self.current_tick = 0

    def arrivals_for_second(self, second: int) -> dict:
        """计算每秒各方向各车道到达数"""
        arrivals = {r: {l: 0 for l in LANES} for r in ROADS}

        if self.scenario == "balanced":
            if second % 3 == 0:
                arrivals["N"]["STRAIGHT"] = 1
                arrivals["S"]["STRAIGHT"] = 1
                arrivals["E"]["STRAIGHT"] = 1
                arrivals["W"]["STRAIGHT"] = 1
            if second % 6 == 2:
                arrivals["N"]["LEFT"] = 1
                arrivals["S"]["LEFT"] = 1
            if second % 6 == 3:
                arrivals["E"]["LEFT"] = 1
                arrivals["W"]["LEFT"] = 1

        elif self.scenario == "ns_rush":
            if second % 2 == 0:
                arrivals["N"]["STRAIGHT"] = 1
                arrivals["N"]["RIGHT"] = 1
                arrivals["S"]["STRAIGHT"] = 1
                arrivals["S"]["RIGHT"] = 1
            if second % 4 == 2:
                arrivals["N"]["LEFT"] = 1
                arrivals["S"]["LEFT"] = 1
            if second % 8 == 0:
                arrivals["E"]["STRAIGHT"] = 1
            if second % 10 == 0:
                arrivals["W"]["STRAIGHT"] = 1

        elif self.scenario == "left_rush":
            if second % 2 == 0:
                arrivals["N"]["LEFT"] = 1
                arrivals["S"]["LEFT"] = 1
            if second % 2 == 1:
                arrivals["E"]["LEFT"] = 1
                arrivals["W"]["LEFT"] = 1
            if second % 5 == 2:
                arrivals["N"]["STRAIGHT"] = 1
            if second % 5 == 3:
                arrivals["E"]["STRAIGHT"] = 1

        elif self.scenario == "alternating_wave":
            if second < 30:
                arrivals["N"]["STRAIGHT"] = 2
                arrivals["S"]["STRAIGHT"] = 2
                arrivals["N"]["LEFT"] = 1
                arrivals["S"]["LEFT"] = 1
            elif second < 60:
                arrivals["E"]["STRAIGHT"] = 2
                arrivals["W"]["STRAIGHT"] = 2
                arrivals["E"]["LEFT"] = 1
                arrivals["W"]["LEFT"] = 1
            else:
                arrivals["N"]["STRAIGHT"] = 1 if second % 4 == 0 else 0
                arrivals["S"]["STRAIGHT"] = 1 if second % 4 == 2 else 0
                arrivals["E"]["STRAIGHT"] = 1 if second % 5 == 0 else 0
                arrivals["W"]["STRAIGHT"] = 1 if second % 5 == 2 else 0

        elif self.scenario == "idle":
            pass

        return arrivals

    def step(self) -> TickData:
        """执行一步模拟"""
        second = self.current_tick
        arrivals = self.arrivals_for_second(second)

        # 入队
        for road in ROADS:
            for lane in LANES:
                self.lane_queues[road][lane] += arrivals[road][lane]

        # 构建车辆列表
        vehicles = []
        for road in ROADS:
            for lane in LANES:
                queue_size = self.lane_queues[road][lane]
                for idx in range(queue_size):
                    self.vehicle_seq += 1
                    vehicles.append(VehicleState(
                        vehicle_id=f"{road}-{lane[0]}-{self.vehicle_seq}",
                        road=road,
                        lane=lane,
                        position=5.0 + idx * 5.0,
                        speed=0.0,
                        is_waiting=True
                    ))

        queues = {r: dict(self.lane_queues[r]) for r in ROADS}

        tick_data = TickData(
            second=second,
            vehicles=vehicles,
            queues=queues
        )

        self.current_tick += 1
        return tick_data


def output_json(timeline: list[TickData], output_file: Optional[str] = None):
    data = {
        "scenario": sim.scenario,
        "duration": sim.duration,
        "ticks": [t.to_json() for t in timeline]
    }
    json_str = json.dumps(data, ensure_ascii=False, indent=2)
    if output_file:
        with open(output_file, "w", encoding="utf-8") as f:
            f.write(json_str)
        print(f"Saved to: {output_file}")
    else:
        print(json_str)


def output_compact(timeline: list[TickData]):
    for tick in timeline:
        queues = {r: sum(tick.queues[r].values()) for r in ROADS}
        print(f"t={tick.second:03d} vehicles={len(tick.vehicles)} queues=" +
              ",".join(f"{r}:{queues[r]}" for r in ROADS))


def output_stream(sim: TrafficSimulator, interval: float = 1.0):
    print("Streaming simulation... (Ctrl+C to stop)")
    print("-" * 60)

    try:
        while True:
            tick = sim.step()
            left = sum(1 for v in tick.vehicles if v.lane == "LEFT")
            straight = sum(1 for v in tick.vehicles if v.lane == "STRAIGHT")
            queues = {r: sum(tick.queues[r].values()) for r in ROADS}

            print(f"[t={tick.second:03d}s] total={len(tick.vehicles)} "
                  f"left={left} straight={straight} | queues: " +
                  " ".join(f"{r}:{queues[r]}" for r in ROADS))

            if tick.second >= sim.duration:
                break
            time.sleep(interval)
    except KeyboardInterrupt:
        print("\nStopped.")


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="交通模拟数据生成器"
    )
    parser.add_argument("--scenario", "-s",
                        choices=["balanced", "ns_rush", "left_rush", "ew_rush", "alternating_wave", "idle"],
                        default="balanced", help="场景类型")
    parser.add_argument("--duration", "-d", type=int, default=120, help="仿真时长（秒）")
    parser.add_argument("--output", "-o", help="输出文件")
    parser.add_argument("--format", "-f", choices=["json", "compact"],
                        default="json", help="输出格式")
    parser.add_argument("--stream", action="store_true", help="流式输出")
    parser.add_argument("--interval", type=float, default=1.0, help="流式输出间隔（秒）")
    return parser


if __name__ == "__main__":
    parser = build_parser()
    args = parser.parse_args()

    global sim
    sim = TrafficSimulator(scenario=args.scenario, duration=args.duration)

    if args.stream:
        output_stream(sim, args.interval)
    else:
        timeline = []
        for _ in range(args.duration):
            tick = sim.step()
            timeline.append(tick)

        if args.format == "json":
            output_json(timeline, args.output)
        else:
            output_compact(timeline)