# F7-02A：自定义组件与有界绘制 Surface v0

目标：让外部应用开发者只用 F7-01 Public PocketUI SDK，编写、注册和复用自定义组件，不修改、重链渲染 Runtime。本阶段**实现的是真实可执行的矩形／轴向线条组合组件，不是原生 Canvas / Path / 任意折线渲染**。

## 原理与当前引擎边界

当前 Pocket 渲染器通过私有 PSC1 Scene Wire 实际支持帧渲染、损伤与定时器；`Engine Contract` 中预留的 Node Tree/Resource 接口并非已启用的绘图能力。F7-02A 不修改 Scene Wire，不新增私有 Renderer ID，也不对开发者暴露 framebuffer、QuickJS JSValue、C/C++ 插件。

Surface v0 组合路径：

    Developer application.js
      → Public PocketUI / PocketCustom
      → bounded rect / horizontal / vertical
      → existing ui.createView / ui.setLayout
      → existing Native component/layout/style
      → existing QuickJS/PocketJS scene/frame

自定义组件实现由外部开发者按 `PocketCustom.register('vendor.widget', factory)` 声明。`mount(...)` 在父 View 内创建一个有界子 Surface。`factory(surface, props)` 可绘制矩形和轴向线条，返回 `update(nextProps)`，只在安全的 Application Owner Tick 触发更新。实例生命周期由**父 Route/Navigation** 负责，当前不声明独立 destroy/unmount 事件，不能将 JS 实例清空当作 Native 资源释放。

## 硬上限与安全假设

- 每 App 最多 8 个注册工厂、4 个已挂载实例，每实例最多 24 个可绘制 View 图元。每次 `rect` 创建和 `move` 修改都验证整数坐标、正尺寸、边界、不允许 NaN/Infinity 和非法 styleRef。
- 每个 Surface 只能包含在自己的声明边界内，不支持溢出绘制；坐标通过现有 Layout/Clip 接口解决。`rect`, `horizontal`, `vertical` 映射到标准 View 组件，颜色来自当前 Theme 和 Style Token。
- 数据与图元数应在 Native 创建前充分校验；若底层因总 Scene 节点预算/其他错误拒绝，现有应用 VM 失败闩锁保持有效，不能把局部场景冒充已成功的原子绘制。
- JS 模块作为**受信应用源码**与当前 `.pui` 合并运行，不是第三方不可信代码沙箱。受限公共 API 只能减少误用；恶意脚本仍需由后续签名、权限隔离与认证保护。
- 不引入像素级 Canvas、Texture Upload、Polyline、任意斜线、Path、Arc、旋转、着色器、视频叠层；也不能用百个密集 Views 冒充高性能曲线。真实折线／仪表弧线必须先完成 F7-02B 的版本化内部 DrawList/Renderer 映射及 P5 实机预算。

## 实际独立示例

`sdk/ui/widgets/bar-chart.js` 实现了一个 8 柱条的复用组件：2 个横竖坐标轴＋8 个条形，正好 10 个图元。`apps/custom-chart/application.js` 只使用公开的 PocketUI + PocketCustom。触摸 Add/Minus 后由下一次 Tick 更新同一 8 个 View 的布局，而不是销毁重建图元；两个分辨率都使用已有 Native 布局解析。

运行当前独立测试：

    make -f scripts/custom-sdk.mk test-custom-surface

在官方 Framework Native + ARM 构建完成、已有受信字体和 catalog 后：

    make -f scripts/custom-sdk.mk test-custom-surface-runtime

这个命令使用现有 `scripts/pui.py` 生成 `com.pocket.custom-chart` 的真实 `.pui`（只请求 `ui.core`）并在原生产 Native/ARM ELF 上以两个软件 Viewport 运行真实触摸 replay。原始 PPM、报告、ELF/包源码 hash 和截图一致性进入 `out/public-custom/real-test/evidence.json`。QEMU/Native 是软件级证明，不能代替 600/800 实机性能或人审。

已有 UI SDK 版本是 `0.1-experimental`；包格式/Runtime API/SDK API 仍是 R3 内部 1，不是正式 Public SDK 1.0。未签名包需要显式开发 opt-in，正式发布者身份/撤销/rollback 与不可信代码隔离留给 #9。

## 后续独立 F7-02B

先给内部 draw protocol 单独确定版本、能力位、操作数、颜色/坐标、像素覆盖、Clip、预算和复杂度，再在已有 Frame/Scene/Renderer 层实现真实 `Polyline`, `Arc`, `Path` 等核心基元。需 Native/ARM 同画面、负向坏包、渲染缓存预算及目标板实测，不能在 F7-02A 偷渡未经校验的 native Engine Resource 能力。

本任务不变更已经正常工作的中文字库、输入法、Goodix 驱动、设备控制、网络中间层和 OTA。R6 #49 继续独立收口。
