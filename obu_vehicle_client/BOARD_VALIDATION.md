# OBU board validation

This copy now follows the new vehicle-RSU protocol:

- no speed
- no position
- no zone-triggered periodic send
- GUI/manual `ENTER` and `LEAVE`
- dynamic `rsu_id`
- SPaT payload uses `light_id/phase/remain/timestamp`

## Board verification target

The board-side validation should prove:

1. `obu_vehicle_client` can start
2. GUI state can hold selected `rsu_id`
3. GUI state can hold selected `from_direction`
4. GUI state can hold selected `to_direction`
5. pressing `ENTER` sends one enter message
6. pressing `LEAVE` sends one pass message
7. received SPaT can update cached light state

## Expected topic rules

### OBU -> RSU

```text
v2i/vehicle/state/{rsu_id}
v2i/vehicle/pass/{rsu_id}
```

### RSU -> OBU

```text
v2i/spat/obu_1
```

## Expected payload rules

### Enter

```json
{
  "vehicle_id": "obu_1",
  "from_direction": "E",
  "to_direction": "straight",
  "timestamp": 1710000001
}
```

### Leave

```json
{
  "vehicle_id": "obu_1",
  "from_direction": "E",
  "to_direction": "straight",
  "status": "passed",
  "timestamp": 1710000008
}
```

### SPaT

```json
{
  "light_id": "light_a",
  "phase": "green",
  "remain": 12,
  "timestamp": 1710000002
}
```

## Serial log checkpoints

Expected key logs after app start:

```text
[INFO][obu_vehicle_client] app started
[NET] connected ssid=...
[MQTT] connect ok host=... port=1883
[MQTT] subscribe topic=v2i/spat/obu_1
```

Expected logs when GUI triggers enter:

```text
[MQTT] topic=v2i/vehicle/state/rsu_a
[MQTT] payload={"vehicle_id":"obu_1","from_direction":"E","to_direction":"straight","timestamp":...}
```

Expected logs when GUI triggers leave:

```text
[MQTT] topic=v2i/vehicle/pass/rsu_a
[MQTT] payload={"vehicle_id":"obu_1","from_direction":"E","to_direction":"straight","status":"passed","timestamp":...}
```

Expected logs when SPaT is injected or received:

```text
[INFO][OBU] cached SPaT light=light_a phase=green remain=12
```

## Handoff contract

The receive path still enters through:

```c
mqtt_client_on_message(&app->mqtt, topic, payload, payload_len);
```

After a valid SPaT update, logic reads:

```c
app->spat.valid
app->spat.updated
app->spat.light_id
app->spat.phase
app->spat.remain_s
```
