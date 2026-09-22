# P3 evdev input

Status: 1024×600 `ilitek_ts` capability 和 Protocol B 已有实机证据；live evdev → PocketJS contacts → framebuffer 已具备软件测试，物理触摸闭环和非对角坐标校准仍待上板。

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

## 坐标 transform：诊断候选，等待非对角点确认

真实 known-point trace 共 650 events、4 个 contact sessions：

- top-left raw `(408,789)` → logical 约 `(25,29)`；
- center raw `(7527,7245)` → logical 约 `(470,265)`；
- bottom-right raw `(15846,15956)` → logical 约 `(989,583)`；
- drag tracking-id 131 从 raw `(2396,6905)` 运行到约 `(12911,14682)`，X/Y 整体随右下方向增长。

下述 1024×600 transform 仅作为诊断候选；这三个近似对角点不足以唯一排除 XY swap：

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

1. 依次点击 A（左上）、B（右上）、C（中心）、D（左下）、E（右下）；
2. 同一位置重复点击，计数应逐次增加；
3. 长按后松手，只计一次点击；
4. 从一个目标拖到另一个目标，不能误计点击；
5. 松手后确认 marker 隐藏，且标记在整个过程中跟随真实手指。

CLI 不写死 event1，而是在 `/dev/input` 中发现符合准入契约的 `ilitek_ts`。测试会生成 touch JSON 和 startup log，并将 input event、input frame、hit query、guest turn、render、present counter 绑定在同一运行记录中。

P3-02 当前不改变 framebuffer mode，不启用 PAN/page-flip。P2B 已证明 VSync capability supported，但 P3 不擅自改变 Presenter 同步策略；该决策归 P5 的测量闭环。

## P4A 边界

物理 keycode 不是 Unicode 文本。本机 P3 交付 pointer primitive，外接键盘 keycode delivery 明确禁用；键盘布局、preedit/composition、candidate、commit 与敏感字段策略归 P4A。

## Acceptance boundary (P3 closure review)

The 650-event trace is preserved byte-for-byte (gzip/base64 plus SHA256) in `tests/input/fixtures/ilitek-trace.json`. Native and ARM gates replay all 249 SYN_REPORTs through InputState, InputBridge and the real PocketJS guest at both logical viewport sizes. This is not a physical 1024x800 test.

**Calibration correction:** top-left/center/bottom-right are approximately collinear. They support positive axis directions but cannot uniquely exclude XY swap. The existing direct mapping remains an explicit diagnostic candidate. Physical admission requires A (top-left), B (top-right), C (center), D (bottom-left), E (bottom-right) on the new page, with the marker at the actual finger position. Do not claim pixel-accurate calibration from approximate touch locations.

The live loop now snapshots kernel slots on open and after overflow, suppresses already-held contacts until all-up, limits each drain to eight 64-event batches, and rediscoveries after disconnect at a 500ms retry cadence. A terminal cancellation discards queued stale edges and reaches the guest before shutdown/reconnect. Move coalescing never changes the original DOWN coordinates. Normal release at unchanged raw coordinates is retained because Protocol B only sends changed axis values.

`--trace-output NEW.csv` records input timestamp, guest begin/end and CPU presentation begin/end, with turn/sample ordinals. Present end does not establish LCD scanout time. Diagnostics store no text, but touch coordinates can reveal selections: only record the dedicated diagnostic screen, never password or business-entry screens. External keyboard keycode delivery remains disabled on this board (only the separate powerkey exists); text and IME belong to #12.

P3 software acceptance consists of all current gates plus the dedicated touch scene and live-loop recovery tests. #22 and #5 remain open until the real 1024x600 touch page is accepted; the 1024x800 board still requires its own hardware profile and acceptance. No CI artifact may turn `physical_touch_validated` to true.
