# P3 evdev input

Status: 1024×600 `ilitek_ts` capability、Protocol B、坐标方向均已由真实设备证据准入；P3-02 正在完成 live evdev → PocketJS contacts → framebuffer 闭环。

## 真实 1024×600 基线

已确认：

- `/dev/input/event0` = `20cc000.snvs:snvs-powerkey`，仅 EV_KEY，不是触摸设备；
- observed `/dev/input/event1` = `ilitek_ts`，EV_KEY + EV_ABS + BTN_TOUCH；
- 主协议：MT Protocol B；
- legacy `ABS_X/Y + BTN_TOUCH` 仅 fallback；
- raw X/Y 与 MT X/Y：0..16384；
- MT slot：0..9，共 10 个 hardware slots；
- PocketJS runtime budget：最多 8 active contacts + 8 terminal cancellations。

event path 只作为证据，不作为运行时身份。生产发现使用 device name + required Protocol-B capabilities。

Capability 证据：`docs/evidence/myimx6ek140-input-20260922.json`。

## 坐标 transform 已准入

真实 known-point trace 共 650 events、4 个 contact sessions：

- top-left raw `(408,789)` → logical 约 `(25,29)`；
- center raw `(7527,7245)` → logical 约 `(470,265)`；
- bottom-right raw `(15846,15956)` → logical 约 `(989,583)`；
- drag tracking-id 131 从 raw `(2396,6905)` 运行到约 `(12911,14682)`，X/Y 整体随右下方向增长。

因此 1024×600 transform 冻结为：

```text
swap_xy  = false
invert_x = false
invert_y = false
```

映射使用 64-bit：

`raw 0..16384 → X 0..1023 / Y 0..599`

Trace 摘要：`docs/evidence/myimx6ek140-input-trace-20260922.json`。

## P3-01 状态机

P3-01 已完成：

- 按 `SYN_REPORT` 提交 coherent frame；
- 正常 `TRACKING_ID=-1` 通过下一 active snapshot 缺失表示 release；
- `SYN_DROPPED` 先向 guest 发 terminal cancellations，再等待 kernel state resync；
- resync 使用 `EVIOCGMTSLOTS` 获取 TRACKING_ID/POSITION_X/POSITION_Y，并用 `EVIOCGABS(ABS_MT_SLOT)` 恢复 current slot；
- drop/disconnect 后 suppress-until-all-up，避免 ghost click；
- >8 active hardware contacts 时 cancel 已发布 guest contacts 并 suppress，避免第 9/10 指 late promotion；
- slot tracking-id replacement 先终止旧 contact，再允许 replacement；
- legacy single-touch fallback 保留但不是主路径。

## P3-02 live runtime

生产 live path：

```text
/dev/input/event*
        ↓
name + Protocol-B capability discovery
        ↓
O_RDONLY | O_NONBLOCK poll/read
        ↓
Protocol B InputState
        ↓
SYN_REPORT
        ↓
InputBridge
  ├─ edge queue
  ├─ move coalescing
  └─ down-edge hitTestBounds once
        ↓
PocketRuntimeContactsInput
        ↓
pocket_runtime_tick_contacts()
        ↓
PocketJS guest/core
        ↓
software raster
        ↓
fbdev Presenter
```

触摸事件不会直接驱动 JS tick。P1 固定的 60Hz guest/core 时钟保持不变；输入只更新 bounded contact queue，每个 guest turn 采样一次。渲染仍按现有每两 turn 一次。

Bridge 会保留 down/up/cancel edge，move-only frame 可以 coalesce；若 bounded queue 溢出则 fail-closed，不静默丢 edge。

## 物理 P3-02 测试

最终静态设备包提供：

```sh
./run-touch-test.sh I_UNDERSTAND_THIS_WRITES_FRAMEBUFFER /dev/fb0 /dev/input 1200
```

约 20 秒内：

1. 点击 TOP LEFT；
2. 点击 CENTER；
3. 点击 BOTTOM RIGHT；
4. 拖动；
5. 松手后确认 marker 立即隐藏，不存在 ghost press。

CLI 不写死 event1，而是在 `/dev/input` 中发现符合准入契约的 `ilitek_ts`。测试会生成 touch JSON 和 startup log，并将 input event、input frame、hit query、guest turn、render、present counter 绑定在同一运行记录中。

P3-02 当前不改变 framebuffer mode，不启用 PAN/page-flip。P2B 已证明 VSync capability supported，但 P3 不擅自改变 Presenter 同步策略；该决策归 P5 的测量闭环。

## P4A 边界

物理 keycode 不是 Unicode 文本。P3 只交付 pointer/key primitive；键盘布局、preedit/composition、candidate、commit 与敏感字段策略归 P4A。
